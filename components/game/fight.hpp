// SPDX-License-Identifier: GPL-3.0-or-later
// Fighting: the Blood Moor's monsters and missiles, the player's combat
// modes (swing, flinch, block, death) and Fighter (components/rules/
// combat.hpp), hits and kills, damage over time, the merc in a fight,
// potions and regeneration, monster sounds.
#pragma once

#include "ai.hpp"
#include "character.hpp"
#include "cues.hpp"
#include "gamedata.hpp"
#include "loot.hpp"

#include <combat.hpp>
#include <d2s_items.hpp>
#include <rules.hpp>
#include <sequences.hpp>
#include <skills.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace d2d::game {

using d2d::rules::ServerDoFunction;
using d2d::rules::ServerStartFunction;

// The skills d2d uses as game.exe does so far (docs/research/re/skills.md):
// the Bash family (srvstfunc 32 builds the record, srvdofunc 2 resolves
// it: Bash, Stun, Concentrate; 6 Power Strike, 39 Berserk, 35 Vengeance
// resolve the same way), Dragon Talon (24 / 42: calc1 kicks), the
// charge-ups (23 / 34, 35: Tiger Strike .. Royal Strike) and Dragon Tail
// (27 / 50: a kick, then fire around the target), Zeal (37 / 13: calc1
// hits), Sacrifice (29 / 64: a hit that costs life) and Smite (- / 150:
// the shield); and on their sequences' hits Jab (5 / 7), Dragon Claw
// (25 / 46), Frenzy (- / 9), Double Swing (- / 70) and Impale (7 / 2);
// Fend (9 / 13) as Zeal, a hit per enemy in reach; Poison Dagger (16 /
// 32: FUN_005c30a0 builds Bash's way with its poison, FUN_005c4cd0
// resolves).
// Every other skill swings a plain attack for now.
// Skills that move the player: Whirlwind (38 / 76), Charge (31 / 67),
// Leap Attack (41 / 78).
// Whether a skill runs this start and do function pair.
inline bool runs(const d2d::rules::Skill& skill, ServerStartFunction start, ServerDoFunction action) {
    return skill.srvstfunc == start && skill.srvdofunc == action;
}
inline bool moving_skill(const d2d::rules::Skill& skill) {
    return runs(skill, ServerStartFunction::kWhirlwind, ServerDoFunction::kWhirlwind) || runs(skill, ServerStartFunction::kCharge, ServerDoFunction::kCharge)
        || runs(skill, ServerStartFunction::kLeapAttack, ServerDoFunction::kLeapAttack);
}
inline bool skill_built(const d2d::rules::Skill& skill) {
    return runs(skill, ServerStartFunction::kBuildHit, ServerDoFunction::kResolveHit) || runs(skill, ServerStartFunction::kElementalStrike, ServerDoFunction::kResolveHit)
        || runs(skill, ServerStartFunction::kBerserk, ServerDoFunction::kResolveHit) || runs(skill, ServerStartFunction::kVengeance, ServerDoFunction::kResolveHit)
        || runs(skill, ServerStartFunction::kDragonTalon, ServerDoFunction::kDragonTalon)
        || runs(skill, ServerStartFunction::kChargeUp, ServerDoFunction::kChargeUp) || runs(skill, ServerStartFunction::kChargeUp, ServerDoFunction::kElementalChargeUp)
        || runs(skill, ServerStartFunction::kDragonTail, ServerDoFunction::kDragonTail)
        || runs(skill, ServerStartFunction::kRepeatedHit, ServerDoFunction::kRepeatedHit) || runs(skill, ServerStartFunction::kSacrifice, ServerDoFunction::kSacrifice)
        || skill.srvdofunc == ServerDoFunction::kSmite
        || runs(skill, ServerStartFunction::kJab, ServerDoFunction::kJab) || runs(skill, ServerStartFunction::kDragonClaw, ServerDoFunction::kDragonClaw)
        || skill.srvdofunc == ServerDoFunction::kFrenzy || skill.srvdofunc == ServerDoFunction::kDoubleSwing
        || runs(skill, ServerStartFunction::kFend, ServerDoFunction::kRepeatedHit) || runs(skill, ServerStartFunction::kImpale, ServerDoFunction::kResolveHit)
        || runs(skill, ServerStartFunction::kPoisonDagger, ServerDoFunction::kPoisonDagger)
        || runs(skill, ServerStartFunction::kElementalStrike, ServerDoFunction::kChargedStrike) || runs(skill, ServerStartFunction::kLightningStrike, ServerDoFunction::kLightningStrike)
        || runs(skill, ServerStartFunction::kBuildHit, ServerDoFunction::kConversion)
        || runs(skill, ServerStartFunction::kStackingStrike, ServerDoFunction::kStackingStrike) || runs(skill, ServerStartFunction::kFireClaws, ServerDoFunction::kResolveHit)
        || skill.srvdofunc == ServerDoFunction::kHunger
        || runs(skill, ServerStartFunction::kRabies, ServerDoFunction::kRabies)
        || moving_skill(skill);
}
// Self casts (right click, no target): their aurastate on the caster for
// auralen frames with its aurastats — do 18 (FUN_005c9480: Holy Shield,
// the Sorceress's armors, Bone Armor, Cyclone Armor, Burst of Speed, Fade,
// Venom), 23 (Energy Shield, Blaze), 25 (Enchant, on the caster), 29
// (Thunder Storm), 47 (Cloak of Shadows).
inline bool self_cast(const d2d::rules::Skill& skill) {
    return (skill.srvstfunc == ServerStartFunction::kHolyShield || skill.srvstfunc == ServerStartFunction::kNone || skill.srvstfunc == ServerStartFunction::kThunderStorm
            || skill.srvstfunc == ServerStartFunction::kBladeShield) && !skill.aurastate.empty()
        && (skill.srvdofunc == ServerDoFunction::kSelfState || skill.srvdofunc == ServerDoFunction::kEnchant || skill.srvdofunc == ServerDoFunction::kThunderStorm
            || skill.srvdofunc == ServerDoFunction::kCloakOfShadows || (skill.srvdofunc == ServerDoFunction::kSelfStateWithMissile && skill.srvmissilea.empty())
            || (skill.srvdofunc == ServerDoFunction::kSelfStateWithMissile && skill.name == "Blaze") || skill.srvdofunc == ServerDoFunction::kShapeShift
            || skill.srvdofunc == ServerDoFunction::kStormAroundCaster || skill.srvdofunc == ServerDoFunction::kBladeShield);
}
inline bool attack_mode(int mode) { return mode == kModeA1 || mode == kModeKK || mode == kModeS1; }
// A finishing move releases charges (FUN_005d5220 runs after Attack's
// srvdofunc and the finishers'): Attack, Dragon Talon, Dragon Tail, and
// each Dragon Claw hit (FUN_005d6340 releases after FUN_005d6200's).
// ponytail: whether Dragon Flight's kick (do 52) releases isn't traced;
// here it doesn't (a level 24 skill: past Act 1).
inline bool finisher(const d2d::rules::Skill* skill) {
    return !skill || skill->id == 0 || skill->srvdofunc == ServerDoFunction::kDragonTalon || skill->srvdofunc == ServerDoFunction::kDragonTail || skill->srvdofunc == ServerDoFunction::kDragonClaw;
}

// The character's level in a skill (the World's for the fight, the skill
// bar's for show): points (a class skill's from the save's skill bytes in
// Skills.txt order; Attack, and a tome's skill with the tome carried, 1),
// then with what the gear gives (gear_props: worn, socketed, set bonuses,
// charms) and `extra` (the skill shrine's +all skills) — only on skills
// that have points.
// ponytail: other general skills and item charges don't count yet.
inline int skill_base_level(const GameData& game_data, const Character& character, int id) {
    const auto& ids = game_data.skills.class_ids[std::size_t(std::max(character.character_class, 0))];
    if (const auto found = std::ranges::find(ids, id); found != ids.end()) return character.stats.skills[std::size_t(found - ids.begin())];
    if (id == 0) return 1;
    const auto* skill = game_data.skills.get(id);
    if (!skill) return 0;
    const char* tome = skill->name == "Book of Townportal" ? "tbk" : skill->name == "Book of Identify" ? "ibk" : nullptr;
    return tome && std::ranges::any_of(character.items, [&](const d2d::d2s::Item& item) { return item.code == tome && item.location == d2d::d2s::item_location::kStored; }) ? 1 : 0;
}
inline int skill_level(const GameData& game_data, const Character& character, int id, const std::vector<d2d::d2s::ItemProp>& extra) {
    const auto* skill = game_data.skills.get(id);
    const int base = skill_base_level(game_data, character, id);
    if (!skill || skill->cls.empty()) return base;
    std::vector<d2d::d2s::ItemProp> props = extra;
    const auto gear = gear_props(game_data, character.items);
    props.insert(props.end(), gear.begin(), gear.end());
    const int bonus = d2d::rules::item_skill_bonus(*skill, std::max(character.character_class, 0), props);
    return base > 0 ? base + bonus : 0;
}

struct Fight {
    const GameData* game_data;
    const Level* const& level;             // Town's: where the player is
    Character& character;
    UnitState& player;
    std::optional<UnitState>& merc;
    const Npc* const& merc_npc;
    d2d::rules::Rng& rng;
    Loot& loot;
    Cues& cues;

    std::vector<Monster> monsters;         // mon_level's (Level::spawns), kept while the game runs
    int next_id = 1;                       // the next monster's unit id
    // A monster by unit id: its index in `monsters`, or -1.
    [[nodiscard]] int monster_index(int id) const;
    void add_monster(Monster monster);

    const Level* mon_level = nullptr;                // whose monsters `monsters` are
    AreaSeen area_seen;                              // their spawn areas' "seen" flags (ai.cpp search_target)
    OpenDoor open_door;                              // a monster's door at a think (the world's doors)
    std::array<std::uint32_t, 2> amplified{};        // the player's / merc's Amplify Damage (a Cursed boss) runs out
    std::uint32_t chilled = 0;                       // the player's chill (state 11, a monster's cold) runs out
    // Chill's -50 attackrate / velocitypercent (FUN_0057af80 -> FUN_00623f50).
    [[nodiscard]] int chill_rate(std::uint32_t now_ms) const { return now_ms < chilled ? -50 : 0; }
    // A healer's and a well's cure (FUN_00578d30 / FUN_00585720): poison
    // (state 2), freeze (state 1: a player's is chill, FUN_0057b230, so
    // none) and the curses (FUN_00578c20, the states flagged at
    // DataTables+0xfc); chill stays. The merc's (FUN_00578ca0 / FUN_005856a0)
    // fills its life too. True when the player lost any.
    // ponytail: the merc's poison isn't kept, nor whether its drink counts.
    bool cure(std::uint32_t now_ms);
    int game_difficulty = 0;                         // new_game's

    std::unordered_map<const Level*, std::vector<Monster>> kept;   // the other levels', while the player is away
    Spawning spawning;                                   // the game's rooms in play and what they spawned (new_game)
    std::vector<Missile> missiles;         // in flight in the Blood Moor
    std::vector<Missile> pending;          // made this frame, joining `missiles` after its update
    std::vector<std::pair<int, std::uint32_t>> strafe;   // Strafe's arrows to come: target, when
    std::vector<std::pair<int, int>> burned;             // this frame's burns: monster, skill
    int channel = -1, last_frame = -1;     // Inferno's skill while its cast lasts
    // The monster being attacked (walked up to, then struck), the player's
    // non-walking mode (A1 attack, GH get-hit, BL block, DT dying, DD dead;
    // -1 none) and when it ends.
    int   attack_mon = -1;
    int   pmode = -1;
    std::uint32_t pmode_until = 0;
    bool  pstruck = false;                 // this swing's hit is resolved
    float prate = 1.f;                     // the mode's animation rate (attack speed, FHR, FBR)
    d2d::rules::Fighter player_combat;                // the player in a fight, as of this frame
    d2d::rules::Fighter pf_kick;           // the same without the weapon (kicks: FUN_00646280 takes it off)
    d2d::rules::StatSum psum{};            // the player's stats (gear, passives) as of this frame
    float cast_x = 0, cast_y = 0;          // where a missile skill was sent
    int aura = 0;                          // the aura on (Town: the right skill when it's one)
    // The player's summons (skills.md "Summons"): a Monster of the skill's
    // summon row on the player's side, the skill that raised it, and the
    // monster it's after.
    // A trap (do 45) shoots instead: its skill (a monster skill whose
    // missile carries the player's), at its level, `shots` times.
    struct Pet {
        Monster monster; int skill = 0; int target = -1; int shot_skill = -1, shot_level = 0, shots = 0;
        // Its sumskills d2d uses (FUN_0056deb0 at their sumsk calcs): a
        // missile it shoots (the skeletal mage's, the hydra's), an aura it
        // runs (Fire Golem's Holy Fire, the totems').
        int ranged = -1, ranged_level = 0, aura = -1, aura_level = 0;
        std::uint32_t aura_next = 0;
        // What the summoning skill's stats gave it (FUN_005c4470).
        std::array<int, 4> res{};                  // fire, lightning, cold, poison
        int thorns = 0, fire_lo = 0, fire_hi = 0, speed_pct = 0;
        const Level* where = nullptr;              // the level it's on (pets follow the player across)
        std::uint32_t until = 0;                   // it goes then (Decoy, Revive, Hydra; 0 never)
        int variant = 0;                           // a skeletal mage's element (its +0xf, FUN_005ce0b0)
        int hits = 0;                              // a Raven's attacks left (0: no count)
        bool idle = false, mirror = false;         // Decoy stands; a Shadow Warrior swings the owner's blow
    };
    std::vector<Pet> pets;
    std::uint32_t aura_next = 0;           // its next pulse
    // The skill the player attacks with (Skills.txt id; 0 Attack), the one
    // this swing uses (Attack when it's not built or can't be paid for),
    // and the strikes still to come (Dragon Talon's kicks, Zeal's hits).
    int   attack_skill = 0, swing_skill = 0, kicks_left = 0;
    std::int64_t self_hurt = 0;            // life (256ths) the player's own skills cost (Sacrifice), taken with the monsters' hits
    // An SQ skill's sequence while it plays (sequences.hpp, for the weapon
    // class): its frames, each seq_frame_ms long, and the hits (event 1)
    // already struck.
    std::span<const d2d::rules::SeqFrame> seq{};
    std::uint32_t seq_frame_ms = 40;
    int seq_struck = 0;
    bool seq_loop = false;                 // Whirlwind's plays over until it arrives
    // A skill that moves the player (Whirlwind, Charge, Leap Attack):
    // toward (tx, ty) at `speed` cells/s while `on`; `fly` ignores walls
    // (a leap); whirl_next is Whirlwind's next hit time (FUN_005d9320).
    struct SkillMove { float target_x = 0, target_y = 0, speed = 0; bool active = false, fly = false; };
    SkillMove smove;
    float move_x = 0, move_y = 0;          // where the click sent a moving skill
    std::uint32_t whirl_next = 0;
    std::vector<int> told;                 // skills logged as not built yet
    // A charge-up's charges (FUN_005d3320: its aurastate, the skill and
    // level in stats 0x15e / 0x15f, the count, at most 3, in aurastat1),
    // until auralencalc ticks after the last one.
    struct Charge { int skill = 0, level = 0, count = 0; std::uint32_t until = 0; };
    std::vector<Charge> charges;
    // Self states from a swing (aurastate): Concentrate's lasts while its
    // swing does (made with no length; its removal isn't traced), Berserk's
    // calc2 ticks (FUN_005d97f0, 10 when that's 0).
    struct SelfState { int skill = 0, level = 0; std::uint32_t until = 0; };
    std::vector<SelfState> self_states;
    int absorb_pool = 0, absorb_skill = -1;        // Bone / Cyclone Armor's damage left to absorb
    std::uint32_t blaze_frame = 0, storm_next = 0; // Blaze's last flame, Thunder Storm's next bolt
    std::uint32_t storm_frame = 0;                 // the last frame buff_tick ran its paced strikes
    std::int64_t bo_life = 0, bo_mana = 0;         // Battle Orders' life and mana on the maxima (256ths)
    // What worn items (and passives) put on the maxima (256ths): the
    // character's own, as a save keeps them, are the maxima less these.
    std::array<std::int64_t, 3> item_max{};        // life, mana, stamina
    // The stats as a save keeps them: the maxima without items or Battle Orders.
    [[nodiscard]] d2d::d2s::Stats own_stats() const;
    // The player's level in a skill: points, and with item bonuses (Town
    // points these at its SkillBar).
    std::function<int(int)> skill_base, skill_level;
    // The merc in a fight: its stats (hireling.txt at its level), life,
    // mode (NU/WL follow, A1 attack, GH, DT) and the monster it's after.
    d2d::rules::MercStats merc_st;
    int   merc_life = 0;
    std::string_view merc_mode = "NU";
    std::uint32_t merc_until = 0;
    bool  merc_struck = false;
    int   merc_target = -1;
    // Potions working: life / mana (8.8 fixed) a millisecond, until when.
    struct Regen { double life = 0, mana = 0; std::uint32_t until = 0; bool poison = false; };
    std::vector<Regen> regen;
    // A shrine's boost (Shrines.txt booster: its stats, FUN_00583b30), one
    // at a time, until when.
    struct Boost { int shrine = 0; std::vector<std::pair<int, int>> stats; std::uint32_t until = 0; } boost;

    // A fresh game at `difficulty`: its monsters, nothing in flight.
    void new_game(int difficulty);
    // The save's merc joins: its fighting stats at its experience.
    void merc_joins();
    // Back in camp after dying: full life, no potions or poison working.
    void revive(std::uint32_t now_ms);

    // Keys 1-4 drink the belt's bottom-row potion in that column: healing
    // and mana potions restore their amount (the class's bonus, a chance
    // of double: rules::potion_amount) over their length, joined with
    // what's left of the last one; a rejuvenation its percentages at once
    // (FUN_005beac0, no bonus).
    // ponytail: the doubling rolls d2d's rng, not the player's own seed
    // (it matters once machines share a game: networking); no shift-click to feed the merc (FUN_00562390: hpot, apot, wpot).
    void drink(int col, std::uint32_t now_ms);
    void drink_item(int id, std::uint32_t now_ms);
    void potion(const std::string& code, std::uint32_t now_ms);
    // Potions and poison, then the steady regeneration: replenish life
    // (hpregen, N/256 a tick) and mana (rules::mana_per_frame, FUN_005806f0:
    // max mana over CharStats ManaRegen seconds, a frame at a time). Poison
    // is negative hpregen, and the player's regen tick (FUN_00580610) never
    // takes life below 1: poison can't kill a player (docs/research/re/combat.md).
    double regen_acc_life = 0, regen_acc_mana = 0;
    void apply_regen(std::uint32_t now_ms, std::uint32_t last_ms);
    // A monster's new mode sounds off (MonSounds.txt): an attack's cry (at
    // its chance) and weapon, get-hit, death, each after its delay in ticks.
    void monster_sounds(Monster& monster, std::uint32_t now_ms);
    // What the character wears (the save's appearance, else the class's starting gear).
    [[nodiscard]] const GameData::Appearance& gfx() const;
    [[nodiscard]] const GameData::AnimTiming& player_anim(int mode) const;
    // A swing takes attack_ticks for the item attack speed and weapon
    // speed; get-hit, block and self cast recover faster with FHR / FBR /
    // FCR (rules::speed_frames on the animation's base frame count).
    void set_pmode(int mode, std::uint32_t now_ms);
    [[nodiscard]] bool dead() const;

    // The player as combat sees them: item stats summed like the char
    // panel's (worn, charms, what's socketed), the weapon and shield worn,
    // the panel's defense and resistances; the passives' stats (`passives`):
    // those with no passiveitype join the sum, a mastery (342..344) only
    // when the weapon is its type, the best one (FUN_00645830), Weapon Block
    // (348) with two claws when either hand is (FUN_0057dca0, class HT2).
    // ponytail: set bonuses and the weapon swap aren't counted; the throw
    // masteries (345..347) wait for thrown weapons.
    [[nodiscard]] d2d::rules::Fighter player_fighter(d2d::rules::Fighter* kick = nullptr,
                                                     const d2d::rules::StatSum* states = nullptr,
                                                     const std::vector<d2d::rules::PassiveStat>* passives = nullptr,
                                                     d2d::rules::StatSum* sum_out = nullptr) const;
    // The player's fighter this frame, with the self states' aurastats
    // (FUN_005c6cc0): skill_armor_percent and armor_override_percent as %
    // of the panel defense (the override after), damageresist to DR %.
    // ponytail: other aurastats aren't read.
    // A friendly aura (srvdofunc 65, FUN_005cf010) puts its aurastate and
    // aurastats on the player (and allies within aurarangecalc subtiles)
    // the same way; its hitpoints (Prayer) heal on each pulse instead.
    // ponytail: resist auras cap at 95, not the panel's max resist; the
    // merc doesn't get them.
    // The self states' aurastats are stats on the player (FUN_005c6cc0):
    // they join the item stats, so toblock (20), damageresist (36) and
    // the rest go through make_fighter; defense % (171 skill_armor_percent,
    // 182 armor_override_percent after it) applies to the panel defense.
    // ponytail: attackrate (68) is taken as IAS and velocitypercent (67) as
    // FRW; other stats make_fighter doesn't read do nothing.
    void update_fighters(std::uint32_t now_ms);

    // What calcs ask of the player (skills.hpp).
    [[nodiscard]] d2d::rules::CalcEnv calc_env();
    // A swing starts: the skill in use when it's built, paid for from mana
    // (FUN_0056c160's cost); short of mana, a skill with AttackNoMana swings
    // a plain attack instead, any other doesn't swing. Its animation (A1,
    // KK for the kicks) at the swing's speed; Dragon Talon's kick count is
    // calc1 (FUN_005d5970).
    bool start_swing(std::uint32_t now_ms);
    // Short of mana for a skill (the client's FUN_00647960 reason 1:
    // FUN_00647540's cost, FUN_006440f0's minmana) the class says so: message
    // 0x15 (table 0x711ddc) -> FUN_004cb9c0, the class table's +0xc,
    // <class>_needmana_1; the same voice again within 75 client frames
    // (DAT_007a0498) is skipped.
    // ponytail: FUN_004b9e90's guard (a voice already playing) isn't kept.
    void need_mana(std::uint32_t now_ms);
    std::uint32_t need_mana_until = 0;
    // An SQ skill plays its sequence (FUN_00663310: seqnum's frames for
    // the weapon class) at the attack's speed; with no frames for the
    // weapon it swings once.
    // ponytail: the sequence's rate taken as the class's A1 (animdata)
    // through attack_ticks; seqinput / seqtrans aren't read.
    void start_sequence(std::uint32_t now_ms);
    // The movement a moving skill starts with (its srvstfunc):
    // Whirlwind (FUN_005d8f50) to the target at the class's walk velocity
    // (FUN_0056e5b0: CharStats +0x40), its sequence over and over;
    // Charge (FUN_005cf6b0) at the monster at run velocity × (max(velocity
    // percent, 50) + par1) / 100; Leap Attack (FUN_005da540) waits for its
    // takeoff event.
    // ponytail: velocitypercent taken as its base 100; Whirlwind's path is
    // a straight line (FUN_0064ea90's pathing isn't traced).
    void start_move(const d2d::rules::Skill& skill, std::uint32_t now_ms);
    // Where the frames stand: the frame index (looping or held on the
    // last) and how many event-1 frames have passed since the start.
    [[nodiscard]] std::size_t seq_at(std::uint32_t now_ms) const;
    [[nodiscard]] int seq_events(std::uint32_t now_ms) const;
    // What the player shows in a sequence: its frame's mode, and a start
    // time that lands the renderer (frame = elapsed / ms_per_frame) on it.
    [[nodiscard]] std::pair<int, std::uint32_t> seq_view(std::uint32_t now_ms) const;
    // The swing's animation: KK for kicks, S1 for Smite, else A1.
    // ponytail: SQ sequences play A1.
    [[nodiscard]] int swing_mode() const;
    // What the swing's skill adds to the blow: the Bash family's toht,
    // calc1 damage %, calc2 damage after, SrcDam, ResultFlags' knockback;
    // a Dragon Talon kick: toht, ln12 damage % (FUN_005d5880), the skill's
    // physical damage, the last kick knocking back (100% on normal
    // monsters, FUN_005d5a30).
    // ponytail: the skills' states (Stun's stun, Concentrate's defense) and
    // calc4's element conversion aren't applied; stat 325's to-hit on kicks
    // isn't added.
    [[nodiscard]] d2d::rules::Swing swing();

    // A hit on monster i (the player's or the merc's): blocked, it blocks;
    // otherwise the damage, life/mana leech (the player's), poison and
    // bleeding over time, a chill, a knockback. A kill counts.
    void land(std::size_t monster_index, const d2d::rules::Blow& blow, bool by_player, std::uint32_t now_ms);

    // Poison and bleeding tick on the monsters near the player.
    void monster_dots(std::uint32_t now_ms, float elapsed);

    // The player's swing: the hit lands on the attack's event frame (at the
    // swing's own speed) — player_blow: hit chance, the monster's block,
    // deadly strike, resistances, elemental damage, crushing blow, leech.
    // The swing's hits: a sequence's on each of its event-1 frames, else
    // one on the attack's event frame.
    void strike(std::uint32_t now_ms);
    void hit(std::uint32_t now_ms, float reach = kMeleeReach + 0.5f);

    // Charged Strike (do 11, FUN_005db850): calc1 of its missile from the
    // monster struck, on away from the player (to twice its spot less the
    // player's), each wandering (FUN_005c9290). Lightning Strike (do 14,
    // FUN_005dbe50): from the monster struck at another within calc1
    // subtiles (FUN_0056bd10), carrying calc2 hops (Chain Lightning's hit 12).
    // ponytail: the wander is a ±40 degree spread, as Charged Bolt's.
    void strike_bolts(const d2d::rules::Skill& skill, std::size_t monster_index, std::uint32_t now_ms);
    // Corpse i goes up (Corpse Explosion, Death Sentry's 'mon death
    // sentry'): its max life x calc1..calc2 %, half fire and half physical,
    // on everything within half the skill's aurarange of it.
    void corpse_blast(const d2d::rules::Skill& skill, int lvl, std::size_t corpse_index, std::uint32_t now_ms);
    // Skills that act where they're cast, with no missile of their own:
    // Psychic Hammer (do 33, FUN_005d3140: the skill's damage on the
    // monster, knocked back at calc1 % — calc2 / 3 / 4 for champions,
    // uniques, bosses), Mind Blast (51, FUN_005d76e0: the skill's physical
    // and stun within aurarange of the point), Static Field (20,
    // FUN_005c9800 -> FUN_005c96a0: calc1 % of each monster's life within
    // aurarange of the caster, as lightning), Corpse Explosion (55,
    // FUN_005c4df0: a corpse's max life x calc1..calc2 %, half fire and half
    // physical, within half its aurarange), Poison Explosion (63,
    // FUN_005c5e60: srvmissilea's cloud on a corpse), Teleport (27,
    // FUN_005ca360: to the point, where it's open; not in town).
    // ponytail: Static Field's floor in Nightmare / Hell, Mind Blast's
    // conversion (Param3 / 4 through FUN_005d7680) and the knockback
    // guards aren't applied; Corpse Explosion takes its corpse's life from
    // the MonStats roll, not FUN_006538a0's.
    [[nodiscard]] static bool spot_skill(const d2d::rules::Skill& skill);
    void spot(const d2d::rules::Skill& skill, std::uint32_t now_ms);
    // The skill's own element on its hit (FUN_0056e0c0: EMin..EMax with
    // brackets, synergy and the element's mastery (flag 1); Power Strike's
    // lightning, Poison Dagger's poison). Stun is elsewhere.
    void skill_element(d2d::rules::Fighter& fighter, const d2d::rules::Skill& skill);
    void self_state(const d2d::rules::Skill& skill, std::uint32_t now_ms);

    // A self cast's state (FUN_005c9480 and kin): auralen frames — none
    // (Bone Armor, Cyclone Armor) lasts until its absorb is spent. Bone /
    // Cyclone Armor fill their pool (stat bonearmor, 256ths); Cloak of
    // Shadows (FUN_005d6630) blinds the monsters within aurarange
    // (auratargetstate cloaked) as long.
    void start_state(const d2d::rules::Skill& skill, int lvl, std::uint32_t now_ms);
    [[nodiscard]] const SelfState* state_of(std::string_view name) const;
    // The player's buffs as the fight goes (their aura events, +0x84):
    // Frozen Armor freezes a melee attacker for calc1 frames (func 2),
    // Shiver Armor hits one with its cold (3), Chilling Armor answers a
    // missile with its bolt at the nearest monster (1); Bone / Cyclone
    // Armor absorb (22 / 25) and Energy Shield takes calc1 % of the damage
    // from mana at calc2 / 16 mana a point (24). Blaze leaves its fire
    // where the player walks; Thunder Storm strikes a monster within 5 cells
    // every 1.6 s.
    // ponytail: the event functions aren't traced (the published effects);
    // Bone Armor takes any damage, Cyclone Armor too (not only elements);
    // Chilling Armor's bolt goes at the nearest monster, not the shooter;
    // Thunder Storm's pace and reach are the published ones.
    int absorb(int damage);
    void buff_events(Foe& foe, std::uint32_t now_ms);
    void buff_tick(std::uint32_t now_ms);
    // The skill states on the monsters (their curse and cry slots): what
    // they add up to this frame. Aurastats damagepercent (25) and
    // velocitypercent (67) go on its damage and pace; Iron Maiden returns
    // calc1 % of its melee damage (event domeleedamage), Terror sends it
    // running, Dim Vision and Cloak of Shadows blind it, Confuse and
    // Attract leave it blind to the player (ponytail: they don't set it on
    // other monsters).
    void monster_states(std::uint32_t now_ms);
    // A charge-up's hit lands: one more charge (up to 3), for auralencalc
    // ticks more (FUN_005d3320).
    void charge(const d2d::rules::Skill& skill, std::uint32_t now_ms);
    // What the charges add to a finishing blow (FUN_005d3ba0 / FUN_005d3ac0,
    // at the higher of the stored level and today's).
    // ponytail: aurastat2's progressive_tohit (par4) isn't given.
    void add_charges(d2d::rules::Fighter& fighter, d2d::rules::Swing& swing);
    // A row with no Skill: its own element at level lvl, the weapon at its
    // SrcDamage.
    [[nodiscard]] static d2d::rules::MissileDamage row_damage(const GameData::MissileInfo& missile_info, int lvl);
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
    //      many subtiles, each a missile of the count's (fire, ice cubes).
    // The count's missile (FUN_005d3cf0): 1 srvmissilea, 2 b, 3 c.
    // ponytail: 37 (Claws of Thunder's bolts, FUN_005d4150), 40 / 41 / 143
    // (Royal Strike, level 30, past Act 1; Fists of Fire's first) are
    // logged once; 39's points come from d2d's rng.
    void release(std::size_t monster_index, std::uint32_t now_ms);
    void prg(const d2d::rules::Skill& skill, ServerDoFunction func, int count, int lvl, float target_x, float target_y, std::uint32_t now_ms);
    // Dragon Tail's kick hit: fire, (calc1 + fire mastery) % of the kick's
    // physical damage, on every monster within aurarangecalc subtiles of
    // the target, less fire resistance (FUN_005d7180 -> FUN_0056bad0).
    // ponytail: fire mastery (stat 329) isn't summed into the fighter; the
    // target is taken to be in the blast.
    void dragon_tail(const d2d::rules::Skill& skill, std::size_t target, int phys, std::uint32_t now_ms);

    // A unique's mods in the fight (uniques.hpp, monsters.md "Boss mods in
    // the fight"), each tick: Lightning Enchanted answers a drop in its life
    // with 8 charged bolts (FUN_005a29a0: 4 ways, 2 each, level mlvl / 2) at
    // most every 10 frames; 4 frames after it dies (the event 7 its death
    // queues), Fire Enchanted's blast and Cold Enchanted's nova
    // (FUN_005a2bd0: coldunique, level mlvl / 2).
    // ponytail: a hit taken in GH waits 2 frames in game.exe, fires at once
    // here; the bolts go straight (Charged Bolt's wander, FUN_005c9290, isn't
    // built), each turned up to 25 degrees at random.
    void boss_events(std::span<Foe> foes, std::uint32_t now_ms);
    // A boss's missile from where it stands, at level mlvl / 2 (at least
    // 1), the row's own element (FUN_0064b100 ..).
    void boss_missile(const Monster& monster, const char* name, float dx, float dy, const std::shared_ptr<std::vector<int>>& ring, std::uint32_t now_ms);
    // Fire Enchanted's death blast (uniques.hpp fire_blast): physical and
    // fire, each a 64th of the roll in 256ths, on everyone within
    // difficulty + 4 subtiles; the corpse's guts (monstercorpseexplode).
    void fire_blast(const Monster& monster, std::span<Foe> foes, std::uint32_t now_ms);

    // Monster i died (the player's or the merc's doing): the player gets the
    // experience, its pack may scatter, it drops its loot.
    // ponytail: the merc's own experience share isn't kept.
    void killed(std::size_t monster_index, std::uint32_t now_ms, bool credit = true);
    struct Kill { int type; float x, y; int level; int super = -1; };   // super: its SuperUniques row
    std::vector<Kill> kills;                       // since the World last looked (its quests' death hooks)

    // The merc as a fighter: its hireling damage, attack rating, defense.
    [[nodiscard]] d2d::rules::Fighter merc_fighter() const;

    // The merc's turn: it goes for the nearest monster within 6 cells of the
    // player that has noticed them (or is within 3 of the merc), strikes in
    // melee — an Act 1 rogue shoots arrows (Missiles.txt arrow) from up to
    // 6 cells — and otherwise follows. Hits use its attack rating against
    // the monster's defense and its damage. Killed, it plays its death and
    // is gone (the save's merc is dead until resurrected).
    // ponytail: mercs' skills and the Hireable AI aren't traced; the rogue's
    // bow is assumed, other mercs fight in melee.
    void merc_turn(std::uint32_t now_ms, float elapsed, const Crowd& crowd);

    // The player's combat modes this frame: dead, the death plays out
    // (true once it's over: a Resurrect may respawn); a swing strikes, and
    // ends — `held` (the left skill sent again), the next follows; a
    // flinch or block ends.
    bool player_modes(bool held, std::uint32_t now_ms, float elapsed);
    // A step of a moving skill. Whirlwind ends where it was sent (or at a
    // wall); Charge follows its monster; a leap flies over whatever's
    // below and lands on its landing event.
    void skill_step(std::uint32_t now_ms, float elapsed);
    // A moving skill's event frame (its srvdofunc):
    // Whirlwind (FUN_005d9580): when its hit timer allows (FUN_005d9320:
    // every 4..16 frames by the attack's speed), one hit (two with two
    // weapons) on the next enemy within 5 (round by id);
    // Charge (FUN_005cf900): running, the monster in reach jumps the
    // sequence to its attack; the attack's event hits (knockback);
    // Leap Attack (FUN_005da7e0): takeoff, landing beside the monster, the
    // strike (FUN_005da660: knockback, a stun on it ends).
    // ponytail: FUN_0062a710's attack frames taken as attack_ticks of the
    // class's A1; FUN_005d92d0 (two weapons) as a weapon in each hand.
    void move_event(const d2d::rules::Skill& skill, std::uint32_t now_ms);
    // FUN_0056bd10 handed the last target: the enemy in reach with the next
    // higher unit id, else the lowest (round the ring; the same one when
    // it's alone).
    void other_target();
    // Impale's price (FUN_005daa40): calc2 % of the time the weapon loses
    // calc3 durability (stat 72); a throwing weapon (FUN_006289f0) one of
    // its quantity (stat 70, FUN_0056c3f0) instead.
    // ponytail: at 0 durability it should break (FUN_0055f850); it just
    // stays at 0.
    void impale_wear(const d2d::rules::Skill& skill);
    // The missile a skill fires, when d2d builds it: its srvmissile after
    // its do (FUN_0056f7f0 with no srvdofunc: FUN_0056ecb0 / FUN_0056ee90
    // -> FUN_0059fa30, from the caster toward the target; srvstfunc 4,
    // FUN_005da8b0, only checks the ammo), or its srvmissilea from its do:
    // 8 (a fan), 17 (Charged Bolt), 22 (a nova), 10 (Guided Arrow, Bone
    // Spirit), 12 (Strafe), 26 (Chain Lightning), 28 (Meteor, Blizzard,
    // Eruption), 24 (Fire Wall), 19 (Inferno, Arctic Blast), 73 (Blessed
    // Hammer), 80 (Fist of the Heavens), 117 (Firestorm), 118 (Twister,
    // Tornado), 123 (Volcano), 43 (Shock Web), 48 (Blade Fury). The row
    // carries the skill's damage (Skill) or none (the weapon's at
    // SrcDamage: Multiple Shot, Strafe); its hit functions: `burst`.
    // `any_owner`: a trap's shot, whose row names the player's skill.
    [[nodiscard]] const GameData::MissileInfo* skill_missile(const d2d::rules::Skill& skill, bool any_owner = false) const;
    [[nodiscard]] bool missile_skill(const d2d::rules::Skill& skill) const;
    // Casting one at (tx, ty): its mana, then its animation (A1 at the
    // attack's speed for the bow skills, else SC with FCR); the missiles
    // leave on the action frame (fire).
    // ponytail: the sequence skills (Lightning's SQ) cast as SC; the delay
    // (+400) isn't kept; no ammo is used up.
    bool cast_missile(int skill, float target_x, float target_y, std::uint32_t now_ms);
    // A skill missile of row `mi` from (x, y) toward (x + dx, y + dy) for
    // `range` ticks (0 velocity: it stays put).
    Missile& launch(const GameData::MissileInfo& missile_info, const d2d::rules::Skill& skill, int lvl, float x, float y, float dx, float dy,
                    int range, std::uint32_t now_ms);
    [[nodiscard]] int calc(const d2d::rules::Skill& skill, const d2d::rules::Calc& calc_row, int lvl);
    // The missiles leave the caster at their velocity for Range + LevRange x
    // level ticks: one toward the cast point; do 8 (FUN_005db410) calc1 of
    // them at points a step apart across the line to it (the step: the
    // line's direction turned a right angle and halved to at most ~1.7
    // subtiles, FUN_0056d3b0), centred on it; do 17 (FUN_005c9300) calc1
    // toward it, each wandering (FUN_005c9290); do 22 (FUN_005c9b50 ->
    // FUN_0056d400) 64 all round, for Range + calc1 ticks.
    // Do 10 (FUN_005db6d0: Guided Arrow, Bone Spirit): one at the monster
    // clicked, which it seeks (missile do 7); calc1 is its damage %
    // (stat 25 through the callback 0x5db6a0). Do 12 (FUN_005dba40,
    // Strafe): st 8 (FUN_005dacd0) counts the monsters within aurarange
    // and clamps that between min(calc3, calc1) and calc1; each do then
    // shoots one at the next of them (FUN_0056bd10), calc2 its damage %.
    // Do 26 (FUN_005ca1b0, Chain Lightning): one carrying calc1 hops. Do 28
    // (FUN_005ca3e0 -> FUN_0056ede0: Meteor, Blizzard, Eruption): the row
    // at the cast point. Do 24 (FUN_005c9ea0, Fire Wall): from the target
    // two makers out across the line from the caster, and srvmissileb on
    // it. Do 19 (FUN_005c8ca0, Inferno): a flame of calc1 ticks per frame
    // while the cast lasts.
    // ponytail: the nova's 64 directions are even angles, not
    // the tables at 0x6e1288 / 0x6e1388; Strafe's arrows go out on a timer
    // (3 ticks apart), not a repeated attack animation; Inferno's channel
    // is its cast's animation (no held button, no mana per frame).
    void fire(const d2d::rules::Skill& skill, std::uint32_t now_ms);
    // A lobbed row that lands (hit function 36: Fire Blast, Shock Web) comes
    // down at its target: its range is the frames to get there.
    [[nodiscard]] static int land_range(const GameData::MissileInfo& missile_info, float dx, float dy);
    // Once a frame for the player's skill missiles (their pSrvDoFunc):
    // 7 (FUN_005ae780) turns a guided one to its target every Param1
    // frames; 6 (FUN_005ae680, Fire Wall's maker) leaves its SubMissile1
    // where it is; 10 / 25 (FUN_005aea60 / FUN_005af880 -> FUN_005a9820:
    // Blizzard, Eruption) every calc2 frames drop SubMissile1 at a random
    // point within calc1 subtiles; 5 (FUN_005ae520: the flames of Fire
    // Wall, Meteor, Blaze) burns whoever stands in it every frame, CollideKill
    // 0. Hit function 14 (FUN_005aabb0, Meteor) goes off where the row's
    // Range ends: the skill's damage on everything within sHitPar1 (else
    // aurarange) subtiles, then HitSubMissile1 at the 18 offsets of
    // 0x6e2550 / 0x6e2598 (every sHitPar2th; FUN_005aaa90), each burning
    // Param3 + Param4 x (level - 1) 256ths a frame (FUN_004cc7c0).
    // Also Strafe's queued arrows and Inferno's flames.
    // ponytail: a burner hits a monster once a frame however many overlap
    // (by skill); the maker leaves one a frame (not per subtile); a Bone
    // Spirit cast at nothing seeks the monster nearest the cast point.
    void missile_tick(std::uint32_t now_ms);
    // An element in 256ths a frame on monster i (a burner's): less its
    // resistance, gathered until whole points come off (no hit recovery).
    void burn(std::size_t monster_index, const d2d::rules::MissileDamage& damage, std::uint32_t now_ms);
    [[nodiscard]] std::array<int, 4> pierce() const;
    // A skill's missile reaching monster i (FUN_0064b860's record): with a
    // weapon share (the skill's SrcDam, or a row with no Skill its
    // SrcDamage) the weapon's blow at that share, attack rating rolled;
    // else a sure hit (ToHit missiles roll the attack rating); then the
    // skill's damage (missile_blow). True: it's spent — a CollideKill
    // missile is, unless it may pierce (Missiles.txt Pierce) and stat 328's
    // chance lets it fly on.
    // Hit function 1 (FUN_005a9a70) explodes instead: every monster within
    // sHitPar1 subtiles (else the skill's calc1) of the missile takes the
    // skill's damage (FUN_0056bad0 -> FUN_0056b7e0: squared subtile
    // distance against the radius squared).
    // ponytail: a wall or the end of its range doesn't set it off; a row
    // with no Skill uses no physical columns (MinDamage..) or synergy.
    bool skill_missile_hits(Missile& missile, std::size_t monster_index, std::uint32_t now_ms);
    // A skill missile's damage on monster i: with a weapon share (the
    // skill's SrcDam, or a row with no Skill its SrcDamage) the weapon's
    // blow at that share, attack rating rolled; else a sure hit (ToHit rows
    // roll the attack rating); then the skill's damage (missile_blow).
    // `freeze`: a Glacial Spike's freeze, frames.
    void strike(const Missile& missile, std::size_t monster_index, std::uint32_t now_ms, int freeze = 0);
    // Every monster within r subtiles of (x, y) takes the missile's damage.
    void area(const Missile& missile, float x, float y, int radius, std::uint32_t now_ms, int freeze = 0);
    // A missile's hit function where it is — with the unit it struck, or
    // none where its Range ran out (FUN_005adf10 calls it either way), once:
    // 1 (FUN_005a9a70) the skill's damage within sHitPar1 (else calc1)
    // subtiles; 3 (bomb on ground) within aurarange; 13 (FUN_005aa8b0,
    // Glacial Spike) within sHitPar1 (else aurarange), freezing sHitPar2
    // (else auralen) frames; 2 / 4 / 36 / 51 (FUN_005a9d80, FUN_005b07a0,
    // FUN_005abf70) HitSubMissile1 there (Plague Javelin's cloud, Exploding /
    // Freezing Arrow's blast, the traps on the ground); 9 (FUN_005aa250,
    // Immolation Arrow) the damage within sHitPar1 (else calc1) and its fire
    // over that ground (FUN_005a9530); 14 (Meteor, FUN_005aabb0) and 47
    // (FUN_005ac550, Molten Boulder) the damage within sHitPar1 (else
    // aurarange) and HitSubMissile1 at 0x6e2550's 18 offsets; 20
    // (FUN_005ab370, Lightning Fury) HitSubMissile1 from here at up to
    // calc1 monsters within aurarange; 22 (FUN_005add20, Fist of the Heavens)
    // the damage within aurarange of its target, then HitSubMissile1 at
    // each undead in it; 29 (FUN_005abb00, Frozen Orb) HitSubMissile1 in
    // every sHitPar1th of 64 directions; 48 (FUN_005ac6d0) HitSubMissile1
    // on toward where it was sent.
    // ponytail: Immolation's ground fire burns the row's EMin a frame (the
    // record FUN_0064b7c0 builds isn't traced); Plague's clouds are one;
    // a wall doesn't set a missile off.
    void burst(Missile& missile, std::uint32_t now_ms);
    // The aura's pulse, every perdelay ticks while it's on: a friendly one's
    // hitpoints heal the player (Prayer: edns, 256ths); an enemy one
    // (srvdofunc 66, FUN_005cf3a0; 81, FUN_005d0920) strikes each monster
    // within aurarangecalc subtiles (FUN_0056b7e0 with aurafilter) with the
    // skill's element (FUN_0056e0c0, the mastery in; Holy Fire, Holy Shock,
    // Holy Freeze's cold) — its aurastats ride the target state (+0x82)
    // and target_of applies them while the monster is in range.
    // ponytail: perdelay taken as ticks (its reader isn't traced); the
    // aurafilter bits aren't read (every monster counts); Sanctuary and
    // Redemption's own callbacks aren't built; no mana is drawn
    // (FUN_00644b10).
    void aura_pulse(std::uint32_t now_ms);
    [[nodiscard]] bool in_aura(const Monster& monster);
    // Monster i as the player's blows see it: an enemy aura's aurastats
    // while it's in range (Conviction: resistances and defense down;
    // resistances 36 physical, 37 magic, 39 fire, 41 lightning, 43 cold,
    // 45 poison, 171 defense %).
    // ponytail: the published rule for immune monsters (a fifth of the
    // cut: Conviction, level 30, past Act 1) isn't applied, nor the other target stats (Holy Freeze's
    // slow: its cold chills instead).
    [[nodiscard]] d2d::rules::Target target_of(std::size_t monster_index);
    void apply_target_stats(const d2d::rules::Skill& skill_row, int lvl, d2d::rules::Target& target);
    // A summoning skill d2d builds (the do spawns the `summon` row through
    // FUN_0056d940): Clay / Blood / Iron / Fire Golem (56, 57), Raise
    // Skeleton and Skeletal Mage (31, from a corpse), Valkyrie (16), the
    // Druid's Raven (114), Spirit Wolf, Fenris and Grizzly (119).
    [[nodiscard]] bool summon_skill(const d2d::rules::Skill& skill) const;
    // A trap's shooting skill: the first of its sumskills that fires a
    // missile d2d builds (the monster skills 'sentry lightning',
    // 'BoltSentry', 'death sentry ltng'); -1 for the others (Wake of Fire's
    // and Inferno's do 125 / 95, Death Sentry's corpse blast do 55).
    [[nodiscard]] int trap_shot(const d2d::rules::Skill& skill) const;
    // Casting one at (tx, ty) (the corpse there for Raise Skeleton): its
    // mana and SC, the summon on the action frame.
    bool cast_summon(int skill, float target_x, float target_y, std::uint32_t now_ms);
    // The unraised corpse within 2 cells of (x, y) nearest it, or -1.
    [[nodiscard]] int corpse_near(float x, float y) const;
    // The pet arrives (FUN_0056d940, FUN_005c49e0, FUN_005c4470): spawned
    // as its MonStats row at the row's level, then set to level min(clvl,
    // clvl x 3 / 4 + bonus) — bonus calc2 for the Druid's (do 114 / 119),
    // else 0 — with MonLvl's own defense and attack rating at it (stats 31
    // / 19), life + calc1 %.
    // Past petmax of its pettype the oldest goes.
    // ponytail: the skill's aurastats / passive stats / sumskills / sumumod
    // on the pet (FUN_005c4470), Skeleton and Golem Mastery (FUN_005d6b60),
    // the skeletal mage's missile and the golems' specials aren't built;
    // pets stay in the Blood Moor.
    // A skill by its name as the tables write it (Fire Golem's sumskill
    // says 'holy fire').
    [[nodiscard]] const d2d::rules::Skill* skill_named(std::string_view name) const;
    // The missile row a pet's skill shoots: its srvmissile, else
    // srvmissilea — NecromageMissile's do 149 (FUN_005ce0b0) adds the mage's
    // variant (+0xf of its monster data) to it: necromage1..4.
    // ponytail: the variant is rolled at the raise; where game.exe sets it
    // isn't traced.
    [[nodiscard]] const GameData::MissileInfo* pet_missile(const d2d::rules::Skill& skill, int variant = 0) const;
    void summon(const d2d::rules::Skill& skill, std::uint32_t now_ms);
    void summon_one(const d2d::rules::Skill& skill, int type, int lvl, const d2d::rules::CalcEnv& env, float x, float y, std::uint32_t now_ms);
    // A trap's think (MonStats AI AssassinSentry / DeathSentry): every
    // aidel ticks, with a monster within aip4 subtiles, it shoots its skill
    // at the nearest — the skill's missile, whose row names the player's
    // skill for the damage (sentrylightningbolt: Lightning Sentry) at the
    // trap's sumskill level; spent after its shots (the skill's Param1,
    // Charged Bolt Sentry's calc4), it dies.
    // ponytail: the sentry AI function isn't traced (aip4 read as the
    // range, the shots from the published counts); it shoots from its
    // spot, in no animation.
    void trap_turn(Pet& pet, std::uint32_t now_ms);
    // A pet as the monsters see it: its defense, its MonStats resistances
    // (traps aren't there to hit).
    [[nodiscard]] Foe pet_foe(const Pet& pet) const;
    void pet_hurt(Pet& pet, int damage, std::uint32_t now_ms);
    // The player crossed from `from` to `to` (shifted by dx, dy): the pets
    // with them come along, traps stay where they were set.
    void pets_cross(const Level* from, const Level* destination, float dx, float dy);
    // A pet's enemy aura (Fire Golem's Holy Fire: srvdofunc 66) pulses
    // round the pet, as the player's does round the player (aura_pulse).
    void pet_aura(Pet& pet, std::uint32_t now_ms);
    // The pets' turn, the merc's way: each goes for the nearest monster
    // within 8 of the player that has noticed them (or within 4 of the
    // pet), strikes it in reach on its A1's action frame (its damage and
    // attack rating against the monster's defense), and otherwise follows
    // the player. A dead one plays its death and is gone.
    // ponytail: every pet fights in melee with A1; the Hireable-style
    // think isn't game.exe's pet AI (not traced).
    void pets_turn(std::uint32_t now_ms, float elapsed, const Crowd& crowd);
    // Holy Shield (FUN_005c9480): the holyshield state for auralencalc
    // ticks, its aurastats (toblock dm56) on the player.
    // ponytail: the aura events (+0x84) and the passive part
    // (FUN_005c6dc0) aren't read; a shield is assumed (itypea1 shie isn't
    // checked). A level 24 skill: past Act 1.
    // Reading a scroll or a tome (Skills.txt 219 / 220): its skill cast,
    // no mana, anywhere but in town (checkfunc 5).
    bool portal_due = false;                       // Town Portal's action frame came
    bool cast_scroll(int skill, std::uint32_t now_ms);
    bool cast(int skill, std::uint32_t now_ms);
    // Frenzy's state (FUN_005d8c70): each hit that lands raises it a level,
    // up to the skill's, for auralencalc ticks; its aurastats (velocitypercent
    // dm34, attackrate dm56) are taken at that level.
    void frenzy(const d2d::rules::Skill& skill, std::uint32_t now_ms);
    // Who the next strike of a chain goes for: the same monster while it
    // lives; Zeal, else the nearest one in reach (FUN_0056bd10's search).
    // ponytail: FUN_0056bd10's pick (it's handed the last target's id) isn't
    // traced: nearest is assumed; Zeal doesn't change targets while one lives.
    // Zeal's and Fend's next hit (do 13) goes round the enemies in reach;
    // Talon's next kick stays on its monster while it lives.
    bool next_target();
    // The monsters in reach, alive, by unit id (d2d's index), as
    // FUN_0056b7e0 walks the rooms around the player.
    // ponytail: the reach taken as melee reach (FUN_0056e510's range isn't read).
    [[nodiscard]] std::vector<int> in_reach() const;
    // Closing in on the monster being attacked: in reach, swing; else the
    // point to walk to (nullopt: nothing to do).
    std::optional<std::pair<float, float>> engage(std::uint32_t now_ms);
    // The monsters around the player, in the crowd that blocks walkers.
    void crowd(Crowd& crowd_out);
    // One frame of the fight outside town (`in_moor`: in the level whose
    // monsters these are), and the merc's turn (following, in town).
    void world(bool in_moor, std::uint32_t now_ms, float elapsed, const Crowd& crowd);
    // Bosses' auras (Aura Enchanted), each at its level within its
    // aurarange: Might, Blessed Aim, Fanaticism add their damage / to-hit %
    // to the monsters there; Conviction cuts the foes' resistances and
    // defense; Holy Fire, Shock and Freeze strike the foes every perdelay.
    // ponytail: Fanaticism's attack rate and Holy Freeze's slow aren't
    // applied; the elements' roll ignores the foes' magic damage reduction.
    void monster_auras(std::vector<Foe>& foes, std::uint32_t now_ms);
    // A magic shrine's missiles from (x, y), at level clvl / 5 (1..8): the
    // Storm Shrine's 16 fireballs (FUN_00582da0, missile 62) toward x = +-5k
    // subtiles (k 1..4, + for odd k), y 5, -10, 15, -20; the Exploding and
    // Poison Shrines' 6 potions (FUN_005830e0 / FUN_00583410, missiles 45 /
    // 48) toward (-6, 6), (-6, -6), (0, 6), (0, -6), (6, 6), (6, -6).
    // ponytail: they hit monsters only (game.exe's are the shrine's own and
    // may hit players too: other players wait for networking); the poison potion's cloud is a burst of its
    // row's poison.
    void shrine_missiles(int code, float x, float y, int clvl, std::uint32_t now_ms);
    // The player went to `to`: an outdoor level's monsters come back, the
    // last one's are kept as they were; nothing in flight follows.
    // Its monsters come as its rooms come into play (rooms_up).
    void enter(const Level* destination);
    // The player at (x, y) in `here`: the rooms coming into play round
    // the player populate (player_moved), their monsters joining `monsters`
    // or the level's `kept`. `arrived`: just came in through a warp.
    // (Named `here` because `level` would shadow the class member.)
    void rooms_up(const Level& here, float x, float y, bool arrived);
};

}  // namespace d2d::game
