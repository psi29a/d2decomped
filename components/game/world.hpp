// SPDX-License-Identifier: GPL-3.0-or-later
// The game server's side (docs/design/multiplayer.md): the world a game
// runs — levels, the player's unit, the merc, NPCs, monsters, missiles,
// ground items, objects, the rolls — stepped by commands from the client
// (protocol.hpp). What the client needs to hear back goes out as Events.
// It runs N players (local co-op); what still stands between it and a
// hosted game is docs/design/multiplayer.md's "Still to change" and
// net-host.md's section 1.
#pragma once

#include "ai/units.hpp"
#include "character.hpp"
#include "character_store.hpp"
#include "cues.hpp"
#include "fight.hpp"
#include "gamedata.hpp"
#include "inventory.hpp"
#include "loot.hpp"
#include "protocol.hpp"

#include <click_spot.hpp>
#include <d2s.hpp>
#include <d2s_items.hpp>
#include <drops.hpp>
#include <hire.hpp>
#include <level_ids.hpp>
#include <light.hpp>
#include <monsters.hpp>
#include <object_ids.hpp>
#include <overlays.hpp>
#include <quest_clock.hpp>
#include <quests.hpp>
#include <quests_act3.hpp>
#include <quests_act4.hpp>
#include <quests_act5.hpp>
#include <rules.hpp>
#include <sequences.hpp>
#include <shake.hpp>
#include <vendor.hpp>

#include <algorithm>
#include <array>
#include <bitset>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <ctime>
#include <expected>
#include <functional>
#include <initializer_list>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <tuple>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>

namespace d2d::game {

// The waypoint (Levels.txt Waypoint) of level `level_id`, -1 none.
int waypoint_index(const GameData& game_data, int level_id);

// When an operated object changes from OP to ON, in simulation ticks.
// -1 means its client-side animation does not reach the transition frame.
int object_op_ticks(const Npc& npc);

// What the server tells the client (the S -> C side, network.md).
namespace ev {
// The player's level changed (`from`): a crossing or warp (`keep_map`:
// the automap stays open) or a respawn.
struct LevelChanged { const Level* from = nullptr; bool keep_map = false; };
// The player put somewhere at once in its level (a Teleport, Dragon Flight:
// FUN_00554ea0's flag 0x10000, the host's S->C 0x15).
struct Reassigned {};
// A Thunder Storm bolt struck (its client do 84, FUN_004e3c50): the
// weather's lightning (FUN_00472c50).
struct Lightning {};
// Client control missile effects, started once on receipt (FUN_0046ba50,
// FUN_0046b290); carried by the native view codec too.
struct QuestQuake { d2d::rules::QuestQuake::Kind kind = d2d::rules::QuestQuake::andariel; };
// Arrived at an object or NPC that opens a panel (game.exe's 0x58 "open
// UI" and the NPC interaction).
// A talk carries the NPC's quest messages for the player (quests.hpp).
struct OpenUI { enum Kind { stash, waypoint, talk, trade, hire } kind = stash; int npc = -1; std::vector<d2d::rules::QuestMsg> quest; };
// An object's message for the player (S->C 0x27 mode 2: the Moldy Tome's
// 127): the speech box plays it, and the client answers it heard.
struct Speech { int npc = -1; int string = 0; };
// A quest's log state sent again (S->C 0x5d, no flags): the Quest Log button.
struct QuestNews { int quest = 0; };
// A quest's music sequence (rules::QuestMusic): the level's song fades out
// now, `sound` plays `jingle_ms` on, the song comes back `back_ms` on (from
// its Block 1 cue with `from_block`); 0 / 0: the sound at once, the song on.
struct MusicCue { int sound = 0; int jingle_ms = 0, back_ms = 0; bool from_block = false; };
// A store buy or sale the server refused, or a merc resurrect's answer (S->C 0x2a's
// result, rules::Trade); `npc` the resurrect's NPC (-1: the store's).
struct TradeResult { d2d::rules::Trade result = d2d::rules::Trade::no_gold; int npc = -1; };
// The ambient's dip to black starts (FUN_0046ae50, rules::AmbientDip): its
// frames and the level it's for.
struct AmbientDip { int fade_in = 0, hold = 0, back = 0, level = 0; };
// The item panel (S->C 0x58 {unit, kind}, FUN_004c0550): 0 an object's
// opens (the orifice, FUN_0059dc70); for an open one 1 shuts it, 4 takes
// the wait box down, 5 the item's gone in (FUN_005852e0), 6 an NPC's
// service done / 7 refused (FUN_00579d60).
struct ServiceResult { int npc = -1; int kind = 1; };
// The Cairn Stones' order, each stone 0..4 (StoneAlpha..StoneLambda), for
// the deciphered scroll (S->C 0x50 subtype 4, FUN_00593cb0).
struct StoneOrder { std::array<int, 5> order{}; };
// A line for everyone in the game, wherever they are (a LAN game's chat
// and news): typed chat, who joined or left, who was slain.
struct Message { enum Kind : std::uint8_t { chat, joined, left, slain } kind = chat; std::string name, text; };
// A unique event (S->C 0x89 {event}, FUN_00545760 to every client): the
// client's quest event table 0x7129d8 runs it; 3, the staff in the orifice
// (FUN_0059dd80): its light show (0x46b100).
struct UniqueEvent { int event = 0; };
}  // namespace ev
using Event = std::variant<ev::LevelChanged, ev::Reassigned, ev::Lightning, ev::QuestQuake, ev::OpenUI, ev::Speech, ev::QuestNews, ev::MusicCue, ev::TradeResult, ev::AmbientDip, ev::ServiceResult, ev::Message, ev::StoneOrder, ev::UniqueEvent>;

// What a client is told after each tick (the S -> C side, network.md):
// everything it draws and clicks on. In-process these are copies; a TCP
// transport sends them as bytes.
// ponytail: monsters go whole (the fields a client needs aren't picked yet);
// the character (cc), the store and the item in hand are still shared with
// the World; levels are pointers into the shared GameData (the same on every
// machine, from the map seed). Waits for networking.
struct View {
    const Level* level = nullptr;
    UnitState player;
    bool running = false, dead = false;
    bool poisoned = false;                 // a monster's poison on the player (the life globe's green, state 2)
    bool chilled = false;                  // a monster's cold on the player (state 11, its blue)
    bool stamina_blue = false;             // a stambarblue state on the player (staminapot, shrine_stamina): the stamina bar full and blue (FUN_004975b0)
    bool stamina_shrine = false;           // state 0x88 (shrine_stamina) on it: the stamina text blue too
    bool uninterruptable = false;          // state 0x36 on it (Fight::uninterruptable: a whirl, a leap): the grid's items red
    // With a healthpot / manapot (state 100 / 0x6a) on the player: the
    // client's stat 0x4a / 0x1a, the percent of the max at the potion's end
    // plus the gear's regen, for the globes' preview (FUN_00496f80 /
    // FUN_00497110; rules::life_end_percent); 0 without.
    int life_preview = 0, mana_preview = 0;
    int curse = -1;                        // a monster's curse on the player (Fight::curses): its skill, -1 none
    d2d::rules::OneShots one_shots;        // the player's one-shot overlays (Hero::one_shots)
    bool stunned = false;                  // a monster's Smite on the player (state 21)
    int pmode = -1;                        // Fight::pmode: A1, GH, BL, DT, DD ... (-1 none)
    float prate = 1.f;
    // A get-hit as the client draws it (FUN_00463390): the server's end, a knockback slide (drawn GH, not stepped), the FHR.
    std::uint32_t pmode_until = 0;
    bool knocked = false;
    int fhr = 0;
    int walk_percent = 100;                // a walk's / run's % (World::walk_percent): its animation's rate
    std::vector<d2d::rules::SeqFrame> seq;   // an SQ skill's frames while it plays
    std::uint32_t seq_frame_ms = 40;
    bool seq_loop = false;
    GameData::Appearance gfx{};               // what the character wears
    struct Merc { UnitState unit; const Npc* npc = nullptr; std::string_view mode; int fhr = 0; d2d::rules::OneShots one_shots; };   // fhr: its faster hit recovery (merc_fighter), for its get-hit's drawn frame
    std::optional<Merc> merc;
    // The Hireling screen's numbers (FUN_0048a990): level, experience and the
    // next level's, strength, dexterity, damage, defense, the resistances
    // (fire, lightning, cold, poison), life now and its most; its MonStats row.
    struct MercSheet {
        int level = 0, str = 0, dex = 0, min = 0, max = 0, defense = 0, life = 0, max_life = 0, cls = 0;
        std::int64_t exp = 0, next = 0;
        std::array<int, 4> res{};
    };
    std::optional<MercSheet> merc_sheet;
    // pettype: its skill's Skills.txt pettype (PetType.txt row); life: percent.
    struct Pet { Npc npc; UnitState unit; std::string_view mode; std::string_view pettype; int life = 100; d2d::rules::OneShots one_shots; int fhr = 0; };   // fhr: a party merc's (its items', merc_gear), for its get-hit
    std::vector<Pet> pets;                 // the player's summons on this level
    std::vector<Pet> party_pets;           // the other players' mercs ("hireable") and summons on it (local co-op; life unused)
    std::vector<Monster> monsters;         // the level's
    // motion_z: in pixels, negative up (gfx.md "The motion record": draw z = -z); a client spark's (client_sparks.hpp).
    struct Shot { const GameData::MissileInfo* info = nullptr; float x = 0, y = 0; int dir = 0; std::uint32_t born = 0; int sound = 0; int anim_rate = 0; int motion_z = 0;
                  std::uint32_t dies = 0; };   // dies: when its Range runs out (a ground fire's frames left, rules::ground_fire_frame), 0 unknown
    std::vector<Shot> missiles;
    int attack = -1, attack_skill = 0;     // the monster the player's attacking (unit id), with what
    std::vector<Loot::GroundItem> ground;  // the level's floor
    struct Fire { float x, y; const Npc* npc; std::uint32_t born = 0; };   // born: its mode's start (0: from the clock)
    std::vector<Fire> fires;
    // The player's town portal where they stand (which 0: where it was cast,
    // 1: its twin in town), leading to level `to`.
    // closed_ms: an object 189's mode 4 (rules::CainQuest::portal_mode), 0 not due.
    struct Portal { float x = 0, y = 0; int destination = 0; std::uint32_t born = 0; int which = 0; std::uint32_t closed_ms = 0; };
    std::vector<Portal> portals;
    // The player's corpses where they stand: their look when they fell.
    struct Corpse { float x = 0, y = 0; int dir = 0; GameData::Appearance gfx{}; int which = 0; };
    std::vector<Corpse> corpses;
    // A joined game's other players (a game.exe host's 0x59 / 0x0f): not
    // replicated, the client sets them after each View (apps/d2d netgame).
    struct OtherPlayer { UnitState unit; int cls = 0; std::string name; std::uint32_t id = 0; GameData::Appearance gfx{}; int mode = -1; bool running = false;
                        std::vector<std::string_view> states; bool ghost = false; int walk_percent = 100; d2d::rules::OneShots one_shots; };   // id: the host's unit; mode: an act, -1 walk / stand; states: the host's on it (States.txt names); ghost: local co-op's dead; walk_percent: as View's (a joined game's: its class's run or walk and chill, no FRW); flash: as View's
    std::vector<OtherPlayer> others;
    // Local co-op's death (docs/design/local-coop.md): the screen's player a
    // ghost; everyone dead (the death screen, Esc back to town); the bodies
    // on the level (a player's id, what they wore, the Resurrect channel's
    // share done and how many channel on it).
    bool ghost = false, all_dead = false;
    struct Body { float x = 0, y = 0; int dir = 0; GameData::Appearance gfx{}; int id = 0, cls = 0; float raised = 0; int raisers = 0; };
    std::vector<Body> bodies;
    // The level's NPCs as they patrol, then each Level::nearby level's in
    // that order (its npcs.size() each, its own cells).
    std::vector<UnitState> npc_states;
    // Who each npc_states entry is: the host's Level::npcs in its order (the
    // level's, then each nearby level's). A machine drawing another's game
    // (a LAN joiner) lays its own lists out as these say: its rooms came up
    // in another order, or not at all (ClientSession::lan_take_npcs, lan-host.md).
    struct NpcKey { int level = 0, hc_idx = -1, object_id = 0; float x = 0, y = 0; int palace = 0, quest = 0; };
    std::vector<NpcKey> npc_keys;
    std::vector<std::pair<int, int>> boost;   // the shrine boost's stats while it lasts
    int boost_code = 0;                    // ... its Shrines.txt Code (its state: rules::shrine_state)
    int aura = 0;                          // the aura that's on
    int gold_lost = 0;                     // goldlost (175) of the last death: the death screen's line
    std::array<int, 2> hand_status{};      // the left / right skill's status (Player::hand_status)
    std::vector<int> buffs;                // skills whose state is on the player (their aurastate: Frozen Armor, Shout ...)
    d2d::rules::Day day;                   // the time of day (lighting, day/night sounds)
    bool den_cleared = false;              // the Den of Evil cleared in this game (its quest state, S→C 0x02)
    bool diablo_red = false;               // Diablo's release (S→C 0x89 event 12) until event 13 (classic, his death): rules::release_light
    int light_bonus = 0;                   // item_lightradius (stat 89) from what's worn: the player's light grows by it
    std::vector<std::pair<int, int>> stat_sum;   // the player's stats the calcs read (gear, passives, masteries, states: Fight::psum), the non-zero ones
    PanelStats panel;                      // the char panel's numbers as the server has them: gear, passives, states, the skill blocks (FUN_004eda20)
    d2d::rules::QuestLogStates quest_log{};      // log states as the server sends them, by quest (FUN_00544190)
    d2d::rules::QuestGameFlags game_quests{};    // the game's quest flags (FUN_00544720): 13 done in this game, 15 closed at the first join
    int den_left = 0;                            // the Den's monsters left (the quest log)
    int arreat_left = 0;                         // Rescue on Mount Arreat's soldiers left (the quest log)
    // The player's own character (what only its owner is told): header,
    // stats, items with their unit ids, the item in hand; the open store's
    // stock and the hire list.
    bool has_character = false;
    d2d::d2s::Header header;
    d2d::d2s::Stats stats;
    std::vector<d2d::d2s::Item> items;
    // FUN_004680a0 returns stock's raw client cursor-item pointer; the typed
    // optional is its d2d counterpart, tested directly at each call site.
    std::optional<d2d::d2s::Item> held;
    std::optional<Store> store;
    std::vector<d2d::rules::MercOffer> hire_offers;
    std::vector<Cues::Cue> sounds;         // the world's sounds cued since the last View
    std::vector<Event> events;             // what the World said since the last View
    // A monster by unit id: its index in `monsters`, or -1.
    [[nodiscard]] int monster(int id) const {
        const auto found = std::ranges::find(monsters, id, &Monster::id);
        return id < 0 || found == monsters.end() ? -1 : int(found - monsters.begin());
    }
};

// The game's step: game.exe's frame, 1000 / 25 ms (FUN_0052fc20,
// docs/research/re/network.md).
constexpr std::uint32_t kTickMs = 40;

// A command and the player who sent it (Fight::Hero::id).
struct Order { int player = 0; Command command; };

struct World {
    // A player in the game (docs/design/local-coop.md): their fight (Hero:
    // unit, character, merc, combat), where they're headed, what they hold
    // and have open, what the World told them.
    struct Player : Fight::Hero {
        bool  running = false;                 // run / walk (game.exe's 0x53 / 0x54)
        int   take_warp = -1;                  // the warp of `level` the player is walking to
        bool  walk_pending = false;            // a walk to an item or warp may be issued once more when it ends (unit +0x150, FUN_00548a50)
        const Level* on_level = nullptr;       // split levels (World::split_levels): the level they're on
        bool  own_screen = false;              // a LAN joiner: drawn on its own machine, so it travels alone; the host's screen's party (pad partners) goes together
        int   interact_npc = -1;               // the object / NPC being walked to
        int   pick_item = -1;                  // the ground item being walked to (its unit id)
        int take_portal = -1;                  // the portal (0..3) the player is walking to
        // The player's corpses (FUN_0057f700): where they fell, what they wore
        // and had in hand, 75% of the experience the death took; at most 16
        // (a death past that makes none: World::make_corpse).
        struct Corpse { const Level* level = nullptr; float x = 0, y = 0; int dir = 0; std::vector<d2d::d2s::Item> items;
                        std::int64_t exp = 0; GameData::Appearance gfx{}; };
        std::vector<Corpse> corpses;
        int take_corpse = -1;                  // the corpse the player is walking to
        int last_pmode = -1;                   // the player's mode at the last tick (death's stages)
        std::int64_t exp_lost = 0;             // what the last death took
        // The client's find lines (FUN_004dcaa0 → FUN_004cc270).
        int voice_level = 0;                   // the level it's in (0x7c89dc), since voice_level_at
        std::uint32_t voice_level_at = 0;
        int voice_checked = 0;                 // the last level looked up (0x7c8a04 / 0x7c88cc)
        std::bitset<1024> voice_found;         // levels whose row has come up this game (0x7c78b8)
        std::uint32_t voice_until = 0;         // when the class's last line ends (its start + its WAV's length)
        std::uint32_t swung_at = 0;            // when the player's last swing sound began (unit +0x7c, FUN_004cb6a0), 0 none
        std::array<int, 3> talking{ -1, -1, -1 };   // NPCs the client has a menu, speech or store open with (they stand)
        // Talk records (by hc_idx) whose menu greeting has played since the
        // player was last out of town: FUN_004b66b0 clears the record's +0x15
        // as it greets, FUN_004b3e10 sets all again as the player leaves town.
        std::vector<int> greeted;
        // The NPCs S->C 0x91 named at the last arrival in a town (rules::NpcReturns::due): a talk
        // record's +0x12, its next menu greeting the return line (FUN_004b3510, FUN_004b66b0).
        std::vector<int> returning;
        // When the Countess's death (unit event 0x25) held the player's
        // complete_tower line (FUN_004dcd40, its fifth argument 0): her
        // treasure's end (0x5c) plays it within 60 s (World::tower_treasure).
        std::optional<std::uint32_t> tower_line_held;
        std::vector<Event> events;             // for the client, since it last looked
        Store store;                           // the NPC window open (npc < 0: none): its stock
        bool had_gidbinn = false;
        std::int64_t gold_lost = 0;            // goldlost (175), as the last death set it
        std::size_t portal_pair = 0;           // their town portal in World::portal: [pair] where it was cast, [pair + 1] in town
        // Local co-op's death (docs/design/local-coop.md): dead, a ghost that
        // walks along while their body lies where they fell, their gear on
        // them; a partner's Resurrect raises them there.
        bool ghost = false;
        struct Body { const Level* level = nullptr; float x = 0, y = 0; int dir = 0; GameData::Appearance gfx{}; float raised = 0; int raisers = 0; };
        std::optional<Body> body;              // raised: the channel's share done (0..1); raisers: those channelling now
        int raising = -1;                      // the body they're channelling on (its player's id), -1 none
        std::uint32_t right_before = 0;        // the right skill Resurrect took the place of (saved instead of it)
        std::uint32_t kept_left = 0, kept_right = 0;   // the hand skills a throwing potion took the place of (player data +0x74 / +0x70, hand_skills)
        // The left / right skill's status as this tick left it (FUN_004d9fc0 -> FUN_00647960:
        // Fight::skill_status, 0 it can go): the client's skill searches run only at 0.
        std::array<int, 2> hand_status{};
        bool out = false;                      // hardcore, dead, Resurrect pressed: the screen takes them out of the game (FUN_0054c0e0)
    };
    using Corpse = Player::Corpse;
    // The players, in join order; [0] is there from the start. `actor` is the
    // one acting now (act_as): the World's, the fight's and the loot's.
    std::vector<std::unique_ptr<Player>> players;
    int next_player_id = 1;                 // add_player's next unit id
    Player* actor = nullptr;
    void act_as(Player& player);
    [[nodiscard]] Player& lead() const { return *players.front(); }
    // A new player's slot: the next unit id, in the fight's list.
    Player& add_player();
    // `each` for every player in turn, acting as them; the one acting
    // before acts again after.
    template <class Each> void each_player(Each&& each) {
        Player& was = *actor;
        for (auto& player : players) if (here(*player)) { act_as(*player); each(*player); }   // split levels: those on this one
        act_as(was);
    }
    // Every player in the game wherever they are (split levels: those on
    // other levels, whose turn this isn't, too), acting as each, with the
    // act they're in: a quest's party share goes by act (FUN_00540510), not
    // by level.
    // The level a player is on (split levels: their own).
    [[nodiscard]] const Level* level_of(const Player& player) const { return split_levels && player.on_level ? player.on_level : level; }
    template <class Each> void each_in_game(Each&& each) {
        Player& was = *actor;
        const auto visit = [&](Player& player) {
            const Level* theirs = split_levels && player.on_level ? player.on_level : level;
            act_as(player);
            each(player, level_act(theirs));
        };
        for (auto& player : players) visit(*player);
        for (Player* player : elsewhere) visit(*player);
        act_as(was);
    }
    // Local co-op's death and Resurrect (docs/design/local-coop.md).
    static constexpr float kRaiseSeconds = 10;           // one channeller's; each more adds as much speed
    static constexpr float kRaiseReach = 2;              // cells from the body
    [[nodiscard]] bool anyone_alive() const;           // of those on the host's screen
    [[nodiscard]] bool shared_screen() const;          // the actor shares the host's screen with another player (local co-op's death: a ghost, no respawn till all are dead)
    void become_ghost(Player& player);
    void revive(Player& player, std::uint32_t now_ms);   // up where they are, full life
    void raise_step(float elapsed, std::uint32_t now_ms);   // the bodies' channels this tick
    // A quest step (a kill's, an object's, a tick's) for the party (local
    // co-op, one screen): what it gave the acting player's quest bits, every
    // other player who hasn't done that quest (bits 0 and 1 clear) gets too.
    // game.exe gives party and area credit quest by quest (quests-act*.md:
    // FUN_00540510's callbacks, the players in the level); one screen puts
    // every player in the level and the party, so it comes to this.
    // kOwnCredit's quests credit each player in their hooks, as game.exe
    // does, and are left alone; what's left is an act's travel (words 7,
    // 0xf, 0x17, 0x1c), which the one screen takes everyone on.
    static constexpr std::array<int, 30> kOwnCredit{ 1, 2, 3, 4, 5, 6, 8, 9, 10, 11, 12, 13, 14, 0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x18, 0x19, 0x1a, 0x1b, 0x23, 0x24, 0x25, 0x26, 0x27, 0x28 };   // Act 1, the opening and Act 2, the opening and Act 3, the opening and Act 4, Act 5
    // Every player in the act neither done nor due gets `bits` (a party pass
    // with no one left out: the mover's own taken already).
    void party_bits_all(int quest, std::initializer_list<int> bits) {
        const int in_act = act();
        each_in_game([&](Player&, int their_act) {
            auto& theirs = quests();
            if (their_act != in_act || d2d::rules::quest::bit(theirs, quest, 0) || d2d::rules::quest::bit(theirs, quest, 1)) return;
            for (const int bit : bits) d2d::rules::quest::set(theirs, quest, bit);
        });
    }
    // The party's share of a step (FUN_00540510; local co-op is one party):
    // each other player in the act (the quest's own act; Act 1's here),
    // neither done nor due, gets `bits`.
    void party_bits(int quest, std::initializer_list<int> bits) {
        const int mover = actor->id, in_act = act();
        each_in_game([&](Player& player, int their_act) {
            auto& theirs = quests();
            if (player.id == mover || their_act != in_act || d2d::rules::quest::bit(theirs, quest, 0) || d2d::rules::quest::bit(theirs, quest, 1)) return;
            for (const int bit : bits) d2d::rules::quest::set(theirs, quest, bit);
        });
    }
    // A quest kill's credit as game.exe passes it over the players
    // (FUN_005537d0): `took_part` for each player `is_near` (who isn't done
    // or due: bits 13, 1, ...); then the party of any who now has bit 13
    // (FUN_00540510; local co-op is one party), each member in `act`
    // neither done nor due getting `party_bits`; then `heard` for the rest
    // (bit 14 to those neither done nor due).
    template <class Near, class TookPart, class Heard>
    void kill_credit(int quest, int in_act, std::initializer_list<int> party_bits, Near&& is_near, TookPart&& took_part, Heard&& heard) {
        namespace quest_bit = d2d::rules::quest;
        std::vector<int> passed;
        bool any = false;
        each_player([&](Player& player) {
            if (!is_near(player)) return;
            took_part(player);
            passed.push_back(player.id);
            any |= quest_bit::bit(quests(), quest, 13);
        });
        each_in_game([&](Player& player, int their_act) {      // the party: in the act, wherever in it (another level's turn too)
            if (!any || their_act != in_act || std::ranges::contains(passed, player.id)) return;
            auto& bits = quests();
            if (quest_bit::bit(bits, quest, 0) || quest_bit::bit(bits, quest, 1)) return;
            for (const int bit : party_bits) quest_bit::set(bits, quest, bit);
        });
        each_in_game([&](Player& player, int) { if (!std::ranges::contains(passed, player.id)) heard(player); });
    }
    template <class Step> void credit_party(Step&& step) {
        if (players.size() == 1) { step(); return; }
        Player& mover = *actor;
        const auto difficulty = std::size_t(std::clamp(mover.character.header.active_difficulty(), 0, 2));
        const auto before = mover.character.header.quests[difficulty];
        step();
        const auto& after = mover.character.header.quests[difficulty];
        for (auto& other : players) {
            if (other.get() == &mover) continue;
            auto& theirs = other->character.header.quests[difficulty];
            for (std::size_t word = 0; word + 1 < theirs.size(); word += 2) {
                const int quest = int(word / 2);
                if (std::ranges::contains(kOwnCredit, quest)) continue;
                if (d2d::rules::quest::bit(theirs, quest, 0) || d2d::rules::quest::bit(theirs, quest, 1)) continue;
                theirs[word] |= std::uint8_t(after[word] & ~before[word]);
                theirs[word + 1] |= std::uint8_t(after[word + 1] & ~before[word + 1]);
            }
        }
    }
    // One screen (local co-op): a move that spreads the party further than
    // kLeashWidth x kLeashHeight screen pixels is undone, for whoever made
    // it (they stand where they were, `before` the moves): the commands'
    // (walks, teleports, a charge's steps), then the fight's (a leap's
    // flight, a knockback's slide, which stop there). A step that narrows a
    // party already too wide is let through.
    static constexpr float kLeashWidth = 620, kLeashHeight = 380;
    void leash(std::span<const std::pair<float, float>> before);
    // The other players come to `destination` round (x, y) with their mercs and pets (one screen, one level.
    void bring_party(const Level* from, const Level* destination, float x, float y, bool keep_map, bool tell = true);
    const GameData* game_data = nullptr;
    const Level* level = nullptr;          // where the player is: the town, the Blood Moor, the Den of Evil
    std::vector<UnitState> npc_states;     // the level's NPCs as they patrol
    // Other levels' NPCs, kept while the player's away; the neighbours'
    // keep patrolling (game.exe runs every unit in the rooms in play).
    std::unordered_map<const Level*, std::vector<UnitState>> other_npcs;
    d2d::rules::Rng rng{ 0x2545f491u };    // rolls (the client's stock, talk topics, gambles, merc offers too)
    Cues  cues{ game_data };                   // world sounds due later
    Loot  loot{ game_data, level, rng, cues };          // on the ground (loot.hpp)
    Fight fight{ game_data, level, rng, loot, cues };   // the fight (fight.hpp)
    bool  autoloot_gold = true;            // d2d: gold walked over goes into the purse (deviations.md #5, --toggle autoloot)
    std::vector<int> not_there;            // levels walked toward that aren't built (logged once)
    struct WirtGold { std::uint32_t next_ms; int left, item_level; };
    std::map<std::pair<const Level*, int>, WirtGold> wirt_gold;
    std::map<std::pair<const Level*, int>, std::uint32_t> operated;   // shrines / chests used: when
    // The fissures' eruptions (object 399, event 8 FUN_00581250): the next
    // one due, and when the one under way goes ON (event 1, FUN_00581490);
    // `stopped` once one fell in OP (bugs.md #52). Started as the player
    // first ticks on its level, from Npc::erupts.
    struct Fissure { std::uint32_t next_ms = 0, on_ms = 0; bool stopped = false; };
    std::map<std::pair<const Level*, int>, Fissure> fissures;
    void fissure_frame(std::uint32_t now_ms);
    struct Door { int mode = 0; std::uint32_t when = 0; };
    std::map<std::pair<const Level*, int>, Door> doors;   // doors used: their mode (0..7), when it changed
    // The orb's mist lands on the Durance's stairs then (hit 35, FUN_005abee0): they go to mode 1.
    struct StairsDue { const Level* level = nullptr; std::uint32_t at_ms = 0; };
    StairsDue durance_stairs_due;
    struct Fire { const Level* level; const Npc* npc; float x, y; std::uint32_t born = 0, gone = 0; };
    std::vector<Fire> fires;               // chest traps 5 / 7 left these burning; the afterglow till `gone`
    bool afterglow_made = false;           // a3q7's record +0xc: the Dark Wanderer's afterglow, once a game
    int afterglow_in = 0;                  // quest steps to it: 2 ticks after he goes (0x5bd4ce -> FUN_005bd390), 0 not running
    bool afterglow_due = false;            // that timer has run: it's made
    // a3q6's record (0x14, a3q6.cpp): +0xc 2 once Mephisto's dead; the
    // bridge's event 7 (FUN_005bcac0) every 24 frames from then.
    std::uint32_t bridge_check = 0;
    // The first player's town portal: [0] where it was cast, [1] its twin in
    // town (FUN_0056d130 / FUN_0056cf40); a new one closes the old pair. [2]:
    // the Cairn Stones' portal to Tristram (object 60, FUN_005a9930), [3]
    // its twin in Tristram back to them; [4] the Arcane Sanctuary's way to
    // the Canyon of the Magi (object 60, FUN_0056d130), [5] its twin; [6]
    // the true tomb's way down to Duriel (object 100, OperateFn 43: one
    // way). Act 5's: [8] Anya's red portal to Nihlathak's Temple (object 60,
    // FUN_0058af90), [9] its twin. [10] the Cube's Cow Portal (object 60,
    // FUN_00594140) in the camp, [11] its twin in the Moo Moo Farm. [12] the
    // Gibbet's town portal when Cain has no room (FUN_00593290 -> FUN_0056d130:
    // nobody's own), [13] its twin in the camp. From [14] on, the other
    // players' town portals, a pair each (Player::portal_pair).
    // A portal's other end is k ^ 1.
    struct Portal { const Level* level = nullptr; float x = 0, y = 0; std::uint32_t born = 0; };
    static constexpr std::size_t kGamePortals = 14, kTemplePortal = 8, kCowPortal = 10, kGibbetPortal = 12;
    // A slot's objects.txt row: 60 (PP, red) for [2..5] and [8..11]
    // (FUN_005a9930, FUN_0056d130, FUN_0058af90, FUN_00594140 make object
    // 60), 100 (SJ) for [6], else 59 (TP, blue).
    [[nodiscard]] static constexpr int portal_object(std::size_t which) {
        if (which == 6) return 100;
        return (which >= 2 && which <= 5) || (which >= 8 && which <= 11) ? 60 : 59;
    }
    bool cow_portal_opened = false;        // this game's (quest record 0xb's flag, FUN_00544720)
    // a5q2's cages' portals (FUN_005888d0: object 189, as Cain's): the freed
    // prisoners' way out, no twin, not selectable.
    std::array<Portal, 3> cage_portal{};
    std::vector<Portal> portal = std::vector<Portal>(kGamePortals);
    std::uint32_t now = 0;                 // the tick's time
    const CharacterStore* characters = nullptr;   // where the character is saved
    // A vendor's stock (the NPC's own inventory, server side): bought items
    // gone, sold ones there to buy back, until the party leaves its town
    // (leave_town). By level id and NPC index: with split levels two towns
    // can trade at once.
    std::map<std::pair<int, int>, Store> stocks;
    // A gamble list whose talk ended while another player still talked to
    // the vendor (FUN_00572f20): by level id, NPC index and player id.
    std::map<std::tuple<int, int, int>, Store> kept_gambles;
    // The hire lists (FUN_00576070), one per act's hire NPC (Kashya, Greiz):
    // rolled when first asked for, a hired offer leaves it, and once none is
    // left it's rolled anew (FUN_00577010).
    std::array<std::vector<d2d::rules::HireEntry>, 5> hire_lists;
    // The act's list (rolled on the game seed when first asked for, and
    // again once none is left for hire) and its offers, entry order.
    std::vector<d2d::rules::HireEntry>& hire_list(int list_act);
    [[nodiscard]] std::vector<d2d::rules::MercOffer> hire_offers(int list_act) const;
    // The player's act (0-based): the act of the level they're on.
    [[nodiscard]] int act() const { return level_act(level); }
    d2d::rules::QuestClock quest_clock;    // the quest timers' clock: a step every 20 frames (FUN_0052d870)
    d2d::rules::DenQuest den;              // this game's Den of Evil
    d2d::rules::AndyQuest andy;            // and Sisters to the Slaughter
    d2d::rules::BurialQuest burial;        // and Sisters' Burial Grounds
    d2d::rules::TowerQuest tower;          // and the Forgotten Tower
    d2d::rules::ToolsQuest tools;          // and Tools of the Trade
    d2d::rules::CainQuest cain;            // and The Search for Cain
    d2d::rules::ActTwoOpening opening2;    // and Act 2's opening
    d2d::rules::RadamentQuest radament;    // and Radament's Lair
    d2d::rules::LamEsenQuest lam_esen;     // Act 3's (quests_act3.hpp)
    d2d::rules::GoldenBirdQuest golden_bird;
    d2d::rules::BladeQuest blade;
    d2d::rules::KhalimQuest khalim;
    d2d::rules::BlackenedTempleQuest temple;
    d2d::rules::GuardianQuest guardian;    // its +0xc 2: Mephisto dead (the hellgate, the bridge)
    d2d::rules::IzualQuest izual;          // Act 4's (quests_act4.hpp)
    d2d::rules::HellforgeQuest hellforge;
    d2d::rules::TerrorQuest terror;
    d2d::rules::SiegeQuest siege;          // Act 5's (quests_act5.hpp)
    d2d::rules::RescueQuest rescue;
    d2d::rules::PrisonQuest prison;
    d2d::rules::BetrayalQuest betrayal;
    d2d::rules::RiteQuest rite;
    d2d::rules::EveQuest eve;
    std::uint32_t tyrael_at = 0;           // a5q6: Baal's light fading (missile 625's end, FUN_0058e920): Tyrael
    std::pair<float, float> baal_fell{};   // where Baal died (cells)
    int ubers_dead = 0;                    // the Tristram ubers dead (FUN_005e0070: game +0x1df0 / +0x1dec / +0x1de8), a bit each
    void eve_update(std::uint32_t now_ms);   // Baal's waves, his flight, Tyrael (a5q6)
    std::uint32_t statues_at = 0;          // a5q5: the statues' event 7 (FUN_0058c0e0), 20 frames on
    int statues_back_in = 0;               // quest steps to a5q5's statues' return timer: 2 ticks (0x58c9da -> FUN_0058bd50), 0 none
    bool statues_back_due = false;         // that timer has run
    bool rite_died = false;                // the player's death on the Summit, taken (FUN_0058d560)
    void rite_update(std::uint32_t now_ms);   // the statues, the Ancients' sleep, a death on the Summit (a5q5)
    void rite_put_back(std::uint32_t now_ms);
    int betrayal_log_in = 0;               // quest steps to a5q4's 8-tick timer (0x58b84a -> 0x58b770), 0 none
    std::size_t cages_seen = 0;               // a5q2's cages (Spawning::rescue_cages) come into the quest so far
    std::map<const Level*, std::size_t> objects_inited;   // init_new_objects: each level's npcs seen so far
    void rescue_update(std::uint32_t now_ms);             // the cages, the prisoners' asks (a5q2)
    void prison_update(std::uint32_t now_ms);             // frozen Anya, her thaw, Nihlathak's throne (a5q3)
    void act5_hide();                                     // who's gone: town Nihlathak, fana, drehyaiced (a5q3)
    bool act5_operate(int npc_index, std::uint32_t now_ms);   // frozen Anya (OperateFn 67)
    std::uint32_t fana_at = 0;             // a5q3: the Frozen River marker's event 7 (FUN_0058a500), 25 frames on
    bool thaw_due = false;                 // a5q3's thaw timer (FUN_0058aab0) runs: every 2 quest steps
    std::pair<const Level*, int> fana{ nullptr, -1 };     // the fana object made (Level::npcs index)
    bool nihlathak_made = false;           // a5q3 +0x92: InitFn 69's Nihlathak (superunique 60)
    int izual_ghost_in = 0;                // quest steps to a4q1's 3-tick timer (0x5b4126 -> 0x5b3e60): his ghost, 0 none
    bool izual_ghost_due = false;          // that timer has run
    const Level* izual_level = nullptr;    // where he died: the ghost goes there (0x5b3eaf: Act 4's map, game +0xc8)
    std::uint32_t diablo_dead_at = 0;      // a4q2's +0x18, classic: the 90 / 95 s end (FUN_005b4be0)
    bool fortress_warp_done = false;       // a4q2's +5: the 90 s step
    bool last_portal_made = false;         // a4q2's +0x40 == 2: Tyrael's portal made this game (InitFn 78)
    // S->C 0x61 (FUN_0053d940): the cinematic the client plays now (its
    // table 0x721d9c: 7 D2X_OUT), -1 none; after the game's save
    // (FUN_0052e2a0: every player).
    int movie = -1;
    bool save_due = false;
    bool end_movie = false;                // the client's DAT_007a0604: ACT04END and the endgame screen on leaving
    bool game_closed = false;              // classic, 95 s on (FUN_00530590), or 90 s without bit 13 (C->S 0x69)
    std::uint32_t gems_at = 0;             // the forge's event 7 (FUN_005b6710): the next round of gems
    int gem_rounds = 0;                    // its data +8: rounds left (4 perfect .. 1 plain)
    int gem_shares = 1;                    // its data +0x14: the players the strike credited, a gem each a round
    bool forge_inited = false;             // the forge's InitFn ran (its room came up)
    // A seal's boss, due at its flash's event 7 (object 131, 27 frames on;
    // FUN_005b5750 -> FUN_00545c30) at the flash's spot, subtiles.
    struct SealBoss { int superunique, x, y; std::uint32_t at; };
    std::vector<SealBoss> seal_bosses;
    [[nodiscard]] d2d::rules::KhalimQuest::Parts khalim_parts() const;
    int gidbinn_in = 0;                    // quest steps to a3q3's timer: 7 ticks from the altar's use (0x5b9c22 -> FUN_005b9a30), 0 none
    bool gidbinn_due = false;              // that timer has run: the guardians are due
    int gidbinn_altar = -1;                // the altar's Level::npcs index
    const Level* gidbinn_level = nullptr;  // and its level: FUN_005b9a30 looks in Act 3's map at the altar's spot
    d2d::rules::TaintedSunQuest tainted;   // and the Tainted Sun
    d2d::rules::HoradricStaffQuest staff;  // and the Horadric Staff
    d2d::rules::ArcaneQuest arcane;        // and the Arcane Sanctuary
    d2d::rules::SummonerQuest summoner;    // and the Summoner
    d2d::rules::SevenTombsQuest tombs;     // and the Seven Tombs
    int flavie_line = 59;                  // Flavie's RogueSignpostGossip as last rolled
    int guard_line = 186;                  // the palace guard's line as last rolled (the talk's, for what's heard)
    [[nodiscard]] d2d::rules::HoradricStaffQuest::Items staff_items() const { return { carries("tr1"), carries("msf"), carries("box"), carries("vip"), carries("hst") }; }
    d2d::rules::QuestBits game_bits{};
    // A joined game: the host runs the quests. d2d's records hear nothing,
    // reward nothing and drop nothing; the client shows the host's.
    bool hosted_quests = false;     // the game's own quest flags (game +0x10f4 +0xc), as Act 2's records set them
    int den_left = -1;                     // its monsters alive when last counted
    int den_log_in = 0;                    // quest steps to the log's "Return to Akara" (8 ticks, LAB_00590230), 0 none
    std::array<bool, d2d::rules::kQuestFlagCount> closed_at_join{};  // quests the first join closed (the game's flag 15, FUN_00546270)
    // The quest flags of the difficulty played.
    d2d::rules::QuestBits& quests();
    // Its congratulations bits.
    std::uint64_t& congrats();
    // Each act's quests at the World's quest hooks (world/quests/actN.cpp), called act by act; a new act adds
    // its file and a call at each hook.
    void act1_log(d2d::rules::QuestLogStates& log, const d2d::rules::QuestBits& bits) const;
    void act2_log(d2d::rules::QuestLogStates& log, const d2d::rules::QuestBits& bits) const;
    void act3_log(d2d::rules::QuestLogStates& log, const d2d::rules::QuestBits& bits) const;
    void act4_log(d2d::rules::QuestLogStates& log, const d2d::rules::QuestBits& bits) const;
    void act5_log(d2d::rules::QuestLogStates& log, const d2d::rules::QuestBits& bits) const;
    // A new game's join: each record's join, the chains.
    void act1_join();
    void act2_join();
    void act3_join();
    void act4_join();
    void act5_join();
    void act5_chain();                     // the records' +0xf0 down a5q1 -> a5q2 -> a5q3 -> a5q4
    void act4_chain();                     // the records' +0xf0 down a4q1 -> a4q3 -> a4q2
    bool act4_operate(int npc_index, std::uint32_t now_ms);   // the forge, the seals, Tyrael's portal
    void cloak_dips();                     // Fight::cloaks: each player's ev::AmbientDip (FUN_004d87e0)
    void diablo_release(std::uint32_t now_ms);   // FUN_005b5230: the sanctuary cleared, Diablo due
    void seal_boss(const Level& where, int superunique, int x, int y);   // a seal's flash's event 7
    // The player changed level, from `from`.
    void act1_enter(const Level& from);
    void act2_enter(const Level& from);
    void act3_enter(const Level& from);
    void act4_enter(const Level& from);
    void act5_enter(const Level& from);
    // The NPC's quest messages, in talk order.
    void act1_talk(std::vector<d2d::rules::QuestMsg>& out, int hc_idx, bool fresh);
    void act2_talk(std::vector<d2d::rules::QuestMsg>& out, int hc_idx, bool fresh);
    void act3_talk(std::vector<d2d::rules::QuestMsg>& out, int hc_idx);
    void act4_talk(std::vector<d2d::rules::QuestMsg>& out, int hc_idx);
    void act5_talk(std::vector<d2d::rules::QuestMsg>& out, int hc_idx);
    // The NPC's talk window closed.
    void act1_talk_closed(int hc_idx);
    void act2_talk_closed(int hc_idx);
    void act3_talk_closed(int hc_idx);
    void act4_talk_closed(int hc_idx);
    void act5_talk_closed(int hc_idx);
    // A message heard from the NPC: its rewards.
    void act1_said(int hc_idx, int message_string);
    void act2_said(int hc_idx, int message_string);
    void act3_said(int hc_idx, int message_string);
    void act4_said(int hc_idx, int message_string);
    void act5_said(int hc_idx, int message_string);
    // A frame of the day clock (40 ms).
    void act1_tick();
    void act2_tick();
    void tyrael_frame();                   // Tyrael's leave test, each frame of his level's turn (act2.cpp)
    // A step of the quest clock (quest_clock): the quest timers.
    void act1_quest_step();
    void act2_quest_step();
    void act3_quest_step();
    void act4_quest_step();
    void act5_quest_step();
    // The NPC's "!" over its head.
    bool act1_alert(int hc_idx);
    bool act2_alert(int hc_idx);
    bool act3_alert(int hc_idx);
    bool act4_alert(int hc_idx);
    bool act5_alert(int hc_idx);
    // A monster's death (its quest death hooks).
    void act1_kill(const Fight::Kill& kill, std::uint32_t now_ms);
    void act2_kill(const Fight::Kill& kill, std::uint32_t now_ms);
    void act3_kill(const Fight::Kill& kill, std::uint32_t now_ms);
    void act3_bind_new_bosses();
    // The quest objects' InitFns on the objects made since the last call (rooms_up adds them room by room).
    void init_new_objects();
    // The gold piles the rooms just up made (Spawning::LevelState::gold): onto
    // their level's floor, lying already. A joined game's come from its host.
    void drop_new_gold();
    void act4_kill(const Fight::Kill& kill, std::uint32_t now_ms);
    void act5_kill(const Fight::Kill& kill, std::uint32_t now_ms);
    // A World tick, after the kills.
    void act1_update();
    void act2_update();
    void act3_update(std::uint32_t now_ms);
    void act4_update(std::uint32_t now_ms);
    void act5_update(std::uint32_t now_ms);
    // Every item of the character has a unit id (new ones get theirs).
    void item_ids();

    // A new game: the player at (start_x, start_y) cells, or the town's start when they're negative.
    World(const GameData* loaded_data, int start_x, int start_y);
    World(const World&) = delete;

    // What the client is told (View).
    [[nodiscard]] int monster_colour(const Monster& monster) const;
    [[nodiscard]] View view() const;

    // The character to the CharacterStore: the save's header with what the game changed (level, when last
    // played, the gear's look), its stats and items.
    std::string save();
    // The acting player's character as a whole .d2s built on `raw` (the save it came from),
    // what save() would write: a LAN joiner's, sent back to its own machine (lan-host.md).
    std::expected<std::vector<std::byte>, std::string> save_bytes(std::span<const std::byte> raw);
    // save()'s header: the level, when last played, the map seed, the look; held items stowed.
    d2d::d2s::Header save_header();
    const std::vector<d2d::d2s::Item>* saved_corpse();   // the corpse's items a save keeps
    void set_act(int act);
    void owe_returns(const Level& town);
    void show_palace();

    // Operating a shrine or a chest.
    void drop_wirt_gold(std::uint32_t now_ms);
    void operate(int npc_index, std::uint32_t now_ms, int force = -1);
    // C->S 0x44 from the orifice's item panel (FUN_005852e0): cmd::ObjectItem.
    void object_item(const cmd::ObjectItem& given, std::uint32_t now_ms);
    int shrine_code(int row) const { return row > 0 && std::size_t(row) < game_data->shrines.size() ? game_data->shrines[std::size_t(row)].code : 0; }
    // The OperateFns `operate` handles; one-shot ones stay used (operated):
    // containers, stands, wells, bookshelves, shrines, traps and doors.
    static bool operable(int operate_fn) {
        using namespace d2d::rules::operate_fn;
        return std::ranges::contains(std::array{ kCasket, kShrine, kUrn, kChest, kBarrel, kExplodingBarrel, kCorpse, kArmorStand, kWeaponRack, kWell, kBookshelf, kTrapObject, kWirtBody,
                                                 kCubeChest, kScrollChest, kShaftChest, kTeleportPad, kLamTome, kHellgate, kGidbinnAltar, kKhalimHeart, kKhalimEye, kKhalimBrain,
                                                 kSewerLever, kSewerStairs, kStairs, kDuranceStairs, kCompellingOrb, kHellforge, kSeal, kSealInfector, kSealDeSeis, kSealVizier, kLastPortal }, operate_fn)
            || is_door(operate_fn);
    }
    // The quests' objects a click walks up to and operates (the quest says
    // what they do): the Cairn Stones, the Gibbet, the Inifuss Tree, the Sun
    // Altar, the Orifice, Horazon's Journal, the Sanctuary's portal; Act 5's
    // altar, its stairs, frozen Anya, Baal's portals and the last portal.
    static bool quest_object(int operate_fn) {
        using namespace d2d::rules::operate_fn;
        return std::ranges::contains(std::array{ kCairnStone, kGibbet, kInifussTree, kSunAltar, kOrifice, kJournal, kSanctuaryPortal,
                                                 kAncientsAltar, kAncientsDoor, kFrozenAnya, kBaalsPortal, kHomePortal }, operate_fn);
    }
    static bool is_door(int operate_fn) {
        using namespace d2d::rules::operate_fn;
        return operate_fn == kDoor || operate_fn == kTrapDoor || operate_fn == kSecretDoor || operate_fn == kSlimeDoor || operate_fn == kGate;
    }
    // A door, trap door, secret door, slime door or gate (rules::door_mode): its new mode,
    // footprint and sound.
    void operate_door(int npc_index, std::uint32_t now_ms);
    // A door to `mode` (0 shut, 1 opening, 2 open...): its footprint and sound.
    void set_door_mode(int npc_index, int mode, std::uint32_t now_ms);
    // A barricade door died: the blocker object on its spot opens (mode 2), its footprint out.
    void barricade_broken(const Fight::Kill& kill, std::uint32_t now_ms);
    // This level's slime doors OP long enough go ON.
    void slime_doors_on(std::uint32_t now_ms);
    // The doors used here back to their mode's footprint (rooms laid again shut them).
    void reopen_doors();
    std::pair<const Level*, std::uint32_t> doors_laid{};   // the level and its Level::lays at the last reopen_doors
    // The player's item_reducedprices (stat 87) off their gear and charms now.
    [[nodiscard]] int reduced_prices() const;
    // A monster's door at its think (Fight::open_door): found, operated in reach.
    void display_swing(int skill, float x, float y, std::uint32_t now_ms);   // a joined game: swing at a host monster, for show
    bool monster_door(const Monster& monster, std::uint32_t now_ms);
    // An exploding barrel: open, it hurts whoever's within 3 subtiles and sets off the unopened ones nearer than
    // 3.
    void explode(int npc_index, std::uint32_t now_ms);
    // An object's blast on a unit at (x, y) of `size` (rules::object_blast
    // on the object's unit seed): none in town nor through a wall. 256ths.
    int blast(const Npc& object, float x, float y, int size, std::int64_t life, int unit_level, int dexterity, int defense);
    // FUN_00622b50 (mask 0x804) from a unit of `size` at subtile (x, y) to
    // `object`: rules::sight_blocked over the missile barrier (0x04) and
    // doors (0x800), the object's own footprint lifted while it tests
    // (FUN_00649560 / FUN_00649400).
    [[nodiscard]] bool wall_to(const Npc& object, int x, int y, int size) const;

    // A chest's trap.
    void spring_trap(int trap, float x, float y, int alvl, std::uint32_t now_ms, int undead = 0);
    // A container's trap as it opens (FUN_00582510): trap 8 with no undead
    // family in act 1 is nothing (FUN_00582250); else its event 4 springs
    // it 35 frames on (FUN_005817a0: rules::trap_on_level, then the table).
    // One whose level the player has left by then is dropped: its trap
    // monster would find no one in range.
    struct Trap { const Level* level; int trap; float x, y; int alvl; std::uint32_t due; bool think = false; };
    std::vector<Trap> traps;
    void arm_trap(int trap, float x, float y, int alvl, std::uint32_t now_ms);
    void spring_traps(std::uint32_t now_ms);
    void trap_think(int trap, float x, float y, int alvl, std::uint32_t now_ms);

    // A fresh game for the character: the Blood Moor's monsters at its
    // difficulty, no loot about.
    void enter(const Character& entering);
    // Another player joins the game under way (docs/design/local-coop.md): their character (full life, as
    // entering), beside the first player, their merc along.
    Player& join(const Character& entering);
    // A later player's quest joins, each act's.
    void join_more();
    // A player leaves: their character saved (save's message, "" written), gone from the game.
    std::string leave(int id);
    // The entering character, refilled (enter / join).
    void take_character(const Character& entering);
    // The save's corpse (its corpse list, FUN_00533850; its x / y unread),
    // laid as the player comes in (FUN_005394a0): by the act town's start,
    // corpse_spot round it, the units there blocking (combat.md "Saving").
    void lay_save_corpse(const Character& entering);

    void new_game();

    // The character's merc (cc.header) next to the player, if alive.
    void spawn_merc();

    // Back in camp after dying: at the town start with full life. Monsters
    // stay as they are.
    void respawn(std::uint32_t now_ms);
    // The player came to `level` from `from`: `from`'s NPC states are kept,
    // `level`'s taken back (made at their start the first time).
    void swap_npcs(const Level* from);
    // The party left `from`: a town's vendors restock at their next opening.
    void leave_town(const Level* from);
    void swap_npc_states(const Level* from);         // swap_npcs without the players' level change hooks
    void stones_up();                      // the Cairn Stones as their room comes up (InitFn 6)
    void stones_portal();                  // their timer's run (FUN_00592d50): the portal to Tristram
    bool stones_pending = false;           // on Stony Field, the stones not up yet
    int stones_portal_in = 0;              // quest steps to the stones' portal: 1 tick (0x59366e -> FUN_00592d50), 0 none

    // Leaving the level: past its edge, collision and drawing already use the level next to it in the act
    // (Level::nearby), so the player walks straight on; once they stand outside this map they belong to that
    // level — everything moves by the offset between the two.
    void cross_level();
    // The Den's monsters after a death.
    void den_count(std::uint32_t now_ms);
    [[nodiscard]] static const char* level_name(const Level& level);

    // Taking a warp (a cave mouth): a click by one walks there; close to it, the player goes to the level it
    // leads to and stands at that level's warp back, at its ExitWalk.
    void use_warp();
    // Through `warp` now (FUN_005550b0): to the level it leads to, at that level's warp back and its ExitWalk.
    void travel_warp(const LevelWarp& warp);
    // A click at (x, y) cells is on the warp (FUN_00466870 case 5 ->
    // FUN_00463ee0): its world pixel within LvlWarp's Select box from the
    // tile unit's subtile ((x - y) * 16, (x + y) * 8, FUN_00643260), edges in.
    [[nodiscard]] static bool warp_clicked(const LevelWarp& warp, float x, float y);
    // Within reach of the warp's tile unit: rules::unit_distance (the
    // player's size 2, a warp unit's 0) under rules::kWarpReach (FUN_00641530).
    [[nodiscard]] static bool warp_close(const LevelWarp& warp, float x, float y);
    bool host_warps = false;   // a joined game: the host takes warps; d2d follows its 0x15 there

    // Into level `to` near (ax, ay): everything with the player (merc, pets)
    // comes along; the automap and monsters are the new level's.
    void arrive(const Level* destination, float arrive_x, float arrive_y, const char* how);
    // Where a warp puts the player: the free spot nearest the other side's warp unit, as
    // FUN_005550b0 -> FUN_0064e7b0 -> FUN_0064dea0 find it (0x555144 to 0x555157; FUN_00554ea0
    // searches again from that spot and finds it free): Level::warp_spot, the subtile itself
    // when free, else the least |dx| + |dy| in the first ring that has one, rings 1 to 49.
    // Level::nearest_free scans each ring in another order and goes out to ring 199, so it lands
    // on another subtile. No spot: nullopt, the warp is not taken (FUN_00554ea0 returns 0).
    [[nodiscard]] static std::optional<std::pair<float, float>> warp_arrival(const Level& level, float unit_x, float unit_y);
    std::vector<d2d::rules::QuestMsg> quest_talk(int hc_idx, bool fresh = true);   // every quest's messages from that NPC (fresh: rolls the guard's line)
    void andariel_died(const Fight::Kill& kill, std::uint32_t now_ms);
    void mephisto_died(const Fight::Kill& kill, std::uint32_t now_ms);
    void mephisto_bridge(std::uint32_t now_ms);
    void blood_raven_died(std::uint32_t now_ms);
    void radament_died(const Fight::Kill& kill, std::uint32_t now_ms);
    [[nodiscard]] std::string killer_name(const Fight::Hero::Striker& killer) const;   // the slain message's (FUN_0049eb10), "" none
    // (x, y) in the room of (from_x, from_y) or one next to it.
    [[nodiscard]] bool near_room(float from_x, float from_y, float x, float y) const;
    void transmute(std::uint32_t now_ms);
    void canyon_portal(std::uint32_t now_ms);
    void durielportal_open(std::uint32_t now_ms);
    void kashya_merc();
    void countess_died(std::uint32_t now_ms);
    // The Countess's treasure (missile 332, towerchestspawner, one a chest
    // of cellar 5: FUN_005954f0): frames left, counting down (FUN_005af300),
    // and its unit seed ({a game seed step, 666}: FUN_00555230 -> FUN_00552df0).
    struct Treasure { const Level* level; int npc; int left; d2d::rules::Rng seed; };
    std::vector<Treasure> treasure;
    void tower_treasure(std::uint32_t now_ms);
    void tower_treasure_made();            // FUN_005954f0
    void stamina_frame();                  // a 40 ms frame's stamina drain / regen
    void replenish_frame(std::uint32_t tick_ms);   // a 40 ms frame of the items' timer 3: replenish durability / quantity (FUN_00562d30)
    void replenish_ground_frame();         // the same for the items on the floors
    void life_update_frame(std::uint32_t tick_ms);   // a 40 ms frame's life update to the client (FUN_0052d980; rules::life_update_frame)
    [[nodiscard]] int walk_percent(const Player& player, std::uint32_t now_ms) const;   // a player's walk % (FUN_00623f50)
    [[nodiscard]] static int walk_percent(const GameData& data, int save_class, bool run, int frw, bool chilled, int velocity_pct = 0);   // the same from its parts
    void footstep_frame(std::uint32_t tick_ms);   // the player's footsteps on this tick (FUN_004caf60)
    void find_voice(std::uint32_t tick_ms);       // the class's find line 62 ticks into a level (FUN_004cc270)
    [[nodiscard]] bool game_quest_done(std::size_t quest) const;   // the game's flag 13 on a quest
    // A town NPC's greeting row, never the one it said last (twenty draws), 0 none.
    int greet_sound(const Npc& npc, int kind);
    std::unordered_map<int, int> greeted_with;   // by MonStats class: its last greeting (the record's +0x10)
    std::unordered_map<int, std::array<int, 2>> group_last;   // by a sound group's first row: its last two plays (+0x64 / +0x68)
    // The quest chain from `quest`'s +0xf0 (d2d::rules::chain).
    void chain(int quest) { d2d::rules::chain(quest, den, burial, cain, tower, tools, andy); }
    // Tools of the Trade: whether the player has the Horadric Malus; the malus stand operated (OperateFn 21).
    bool holding_malus() const;
    void malus_stand(int npc_index, std::uint32_t now_ms);
    // The Search for Cain's objects (the tree, the stones, the Gibbet).
    void cain_operate(int npc_index, std::uint32_t now_ms);
    // Tristram Cain (FUN_005e7880, AI NpcOutOfTown, with the a1q4 record's
    // +0x67 / +0x9c target, +0x95 portal): `npc` his Level::npcs slot in
    // Tristram (-1: not out), `stage` the AI data's +0x14 (-1: the Gibbet
    // still opening), `tries` +0x18, `next` his next think.
    // Subtiles: where he heads (AI +0xc / +0x10) and the noted spot (quest +0x9c / +0xa0).
    struct CainWalk { int npc = -1, stage = -1, tries = 0, x = 0, y = 0, spot_x = 0, spot_y = 0; std::uint32_t next = 0; };
    CainWalk cain_walk;
    Portal cain_portal{};                  // his portal (object 189, at the spot): no twin, not selectable
    std::uint32_t cain_portal_closes = 0;  // when it reaches mode 4 (rules::CainQuest::portal_closed_frame); 0: not due
    void cain_step(std::uint32_t now_ms, float elapsed);
    // Whether the player carries an item of `code`.
    [[nodiscard]] bool carries(std::string_view code) const;
    void take_carried(std::string_view code);
    void open_act(int act);                                    // FUN_00538680: the save's progression
    void class_cue(std::string_view line, std::uint32_t at_ms);   // the class's voice: <class>_<line>
    // The compelling orb's break: its mist to object 386 (FUN_005dfee0); when it lands, now_ms without one.
    std::uint32_t orb_mist(const Monster& orb, std::uint32_t now_ms);
    bool place_camp_cain();                                        // camp Cain made by his start object (FUN_00592960); false: no spot
    bool camp_cain_placed = false;                                 // placed since the town's NPC states were last current
    void camp_cain_state();                                        // those states take his new spot once they're current
    void music_cue(std::string_view sound);                       // one-shot quest music
    void music_cue_near(std::string_view sound, float x, float y);   // to each player whose room or one next to it holds (x, y)
    void give_item(std::string_view code, std::uint32_t now_ms, int ilvl = 1, int quality = 2);
    // An item into a player's inventory (event 4, 0x543d80(game, player, item)):
    // only the records the item is bound to as it's made (Items.txt quest - 1,
    // FUN_00555d20 at 0x555d82), as FUN_005439a0 walks the item's list (+0x74).
    // Raised by the ground pickup into a body slot or the bags (FUN_00563560:
    // FUN_00562e00, FUN_005600a0; not the belt or a stack), taking one's corpse
    // (FUN_00562f30, FUN_005600a0), the death putting what was worn and held on
    // the corpse (FUN_0057f700) and a save's corpse made as it loads
    // (FUN_005335e0 mode 1 -> FUN_00531520); not by a quest's gift
    // (FUN_005466b0 places with FUN_00560200) nor a move from the cursor into a
    // grid: those are FUN_00543d80's only callers. `corpse`: the event's player
    // is the corpse (EDX at 0x57f9c1 / 0x531625), a player unit whose quest
    // records are new and clear (0x55536f -> FUN_005348c0 -> FUN_00621f90 ->
    // FUN_0065c430).
    void item_event(std::string_view code, std::uint32_t now_ms, bool corpse = false);
    void item_drop_event(std::string_view code);   // event 5: an item the player dropped (0x543db0)
    void set_waypoint(int index) { if (index >= 0 && index < 40) actor->character.header.waypoints[std::size_t(actor->character.header.active_difficulty())][std::size_t(index >> 3)] |= std::uint8_t(1 << (index & 7)); }

    // Dying, killed by a monster: - experience: DifficultyLevels DeathExpPenalty % of the.
    void death_penalty(std::uint32_t now_ms);
    // The death played out: a corpse where the player lies with what they wore and held, and 75% of the
    // experience lost.
    void make_corpse(std::uint32_t now_ms);
    // Taking one's corpse.
    void take_corpse_items(std::size_t corpse_index, std::uint32_t now_ms);

    // Reading a Scroll of Town Portal or a Tome's charge.
    void read_portal(std::vector<d2d::d2s::Item>::iterator scroll, std::uint32_t now_ms);
    // Town Portal's action frame: a portal by the player and its twin at the town's portal spot; the old pair
    // goes.
    void open_portal(std::uint32_t now_ms);
    // A town portal pair `pair` closes: an end on the Summit counts out of the Rite (RiteQuest::portal_closed);
    // a player walking to either end stops taking it (take_portal -1).
    void close_portal_pair(std::size_t pair);
    bool open_portal_at(float portal_x, float portal_y, std::uint32_t now_ms, int pair = -1);   // -1: the actor's own pair
    // FUN_0056d130's spot (FUN_0064e810, field_spot): from (x, y) cells, a
    // 3 x 3 box (size 3) clear of 0x3e01 and expfield.d2's walk back clear of
    // 0xc01, over the grid, the items lying (0x200) and the units here (the
    // Crowd's 0x1000 / 0x2000). None: nullopt.
    [[nodiscard]] std::optional<std::pair<float, float>> portal_spot(const Level& where, float x, float y);
    // Who's in whose way on the player's level now: the players, their
    // mercs, the monsters and pets about (Fight::crowd).
    [[nodiscard]] Crowd crowd_now();
    // Walking into one: out by the other.
    void use_portal(std::uint32_t now_ms);

    // NPC deals (protocol.hpp): the windows, buying, selling, repairing,
    // identifying, hiring. True when `c` was one.
    bool deal(const Command& command, std::uint32_t now_ms);
    // A belt put on or taken off: what the boxes it lacks held, to the inventory or the floor.
    void spill_belt(std::uint32_t now_ms);
    // A chest round's drops put as they're rolled, round `object` on `onto` (null: this level).
    [[nodiscard]] d2d::rules::Placement chest_place(const Npc& object, int here, std::uint32_t now_ms, const Level* onto = nullptr);
    // After a change of gear: each hand skill fits what the hands hold now.
    void hand_skills(std::uint32_t now_ms);
    // Each player's Player::hand_status, at the end of a level's tick.
    void hand_statuses(std::uint32_t now_ms);

    // A command from the player, checked and applied.
    void apply(const Command& command, std::uint32_t now_ms);

    // One step of the game: the player's commands, then the world. The
    // player walks its path (type 7) to the target (and operates or talks on
    // arrival); NPCs patrol; the merc follows; the monsters and missiles
    // (Fight::world); crossing into the next level or through a warp;
    // potions and regeneration.
    const Level* wanted_near = nullptr;               // whose neighbours were last asked for
    d2d::rules::Day day;
    std::uint32_t day_at = 0;                         // when the day last stepped
    void tick(std::span<const Order> orders, std::uint32_t now_ms, std::uint32_t last_ms);
    // LAN hosting (lan-host stage 9): each player on their own level. tick
    // takes turns, a level with players on it at a time (swap_to; `players`
    // and the fight's heroes are just theirs meanwhile), the lead's level
    // last so the host's screen draws what was ticked. `turn_done` runs at
    // each turn's end with that level in (the host's joiners' Views).
    // A player who leaves their level mid turn goes alone (go_alone): their
    // new level comes in on its own turn. Off: one level, warps move the
    // party (local co-op, one screen).
    bool split_levels = false;
    std::function<void()> turn_done;
    std::vector<Player*> elsewhere;                  // during a turn: the players on other levels (each_in_game)
    // Lines for everyone in the game (ev::Message), whatever level they're
    // on: handed to every player as the tick ends.
    std::vector<ev::Message> announced;
    void announce(ev::Message message) { announced.push_back(std::move(message)); }
    void hand_out_announced();
    void swap_to(const Level* destination);
    void go_alone(const Level* destination, float arrive_x, float arrive_y, const char* how);
    [[nodiscard]] bool here(const Player& player) const { return !split_levels || !player.on_level || player.on_level == level; }
    void tick_level(std::span<const Order> orders, std::uint32_t now_ms, std::uint32_t last_ms);
    std::vector<std::uint32_t> day_frames;           // this tick's day frames, for every turn's players (the day steps once)
    bool first_turn = true;

};

}  // namespace d2d::game
